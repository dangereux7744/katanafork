/*
  File: elfwriter.c
  Author: James Oakley
  Copyright (C): 2011 Dartmouth College
  License: Katana is free software: you may redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 2 of the
    License, or (at your option) any later version. Regardless of
    which version is chose, the following stipulation also applies:
    
    Any redistribution must include copyright notice attribution to
    Dartmouth College as well as the Warranty Disclaimer below, as well as
    this list of conditions in any related documentation and, if feasible,
    on the redistributed software; Any redistribution must include the
    acknowledgment, “This product includes software developed by Dartmouth
    College,” in any related documentation and, if feasible, in the
    redistributed software; and The names “Dartmouth” and “Dartmouth
    College” may not be used to endorse or promote products derived from
    this software.  

                             WARRANTY DISCLAIMER

    PLEASE BE ADVISED THAT THERE IS NO WARRANTY PROVIDED WITH THIS
    SOFTWARE, TO THE EXTENT PERMITTED BY APPLICABLE LAW. EXCEPT WHEN
    OTHERWISE STATED IN WRITING, DARTMOUTH COLLEGE, ANY OTHER COPYRIGHT
    HOLDERS, AND/OR OTHER PARTIES PROVIDING OR DISTRIBUTING THE SOFTWARE,
    DO SO ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND, EITHER
    EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
    PURPOSE. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE
    SOFTWARE FALLS UPON THE USER OF THE SOFTWARE. SHOULD THE SOFTWARE
    PROVE DEFECTIVE, YOU (AS THE USER OR REDISTRIBUTOR) ASSUME ALL COSTS
    OF ALL NECESSARY SERVICING, REPAIR OR CORRECTIONS.

    IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING
    WILL DARTMOUTH COLLEGE OR ANY OTHER COPYRIGHT HOLDER, OR ANY OTHER
    PARTY WHO MAY MODIFY AND/OR REDISTRIBUTE THE SOFTWARE AS PERMITTED
    ABOVE, BE LIABLE TO YOU FOR DAMAGES, INCLUDING ANY GENERAL, SPECIAL,
    INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OR
    INABILITY TO USE THE SOFTWARE (INCLUDING BUT NOT LIMITED TO LOSS OF
    DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY YOU OR
    THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER
    PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGES.

    The complete text of the license may be found in the file COPYING
    which should have been distributed with this software. The GNU
    General Public License may be obtained at
    http://www.gnu.org/licenses/gpl.html

  Project: Katana
  Date: January 2011
  Description: routines for building an elf file for a patch-> Not thread safe
*/

#include "elfwriter.h"
#include <assert.h>
#include <fcntl.h>
#include "util/logging.h"
#include "constants.h"
#include "elfutil.h"


//global because libdwarf gives us no way of passing data into
//dwarfWriteSectionCallback. This means we cannot be concurrently
//generating two patch objects
static ElfInfo* patch=NULL;

typedef struct
{
  Elf_Scn* scn;
  Elf_Data* data;
  //size_t allocatedSize;//bytes allocated for data->d_buf
} ScnInProgress;

//todo: what is this actually used for?
static ScnInProgress scnInfo[ERS_CNT];




//returns the offset into the section that the data was added at
addr_t addDataToScn(Elf_Data* dataDest, const void* data,int size)
{
  dataDest->d_buf=realloc(dataDest->d_buf,dataDest->d_size+size);
  MALLOC_CHECK(dataDest->d_buf);
  memcpy((byte*)dataDest->d_buf+dataDest->d_size,data,size);
  dataDest->d_size=dataDest->d_size+size;
  elf_flagdata(dataDest,ELF_C_SET,ELF_F_DIRTY);
  return dataDest->d_size-size;
}

//wipes out the existing information in dataDest and replaces it with data
void replaceScnData(Elf_Data* dataDest,void* data,int size)
{
  dataDest->d_buf=malloc(size);
  MALLOC_CHECK(dataDest->d_buf);
  memcpy((byte*)dataDest->d_buf,data,size);
  dataDest->d_size=size;
  elf_flagdata(dataDest,ELF_C_SET,ELF_F_DIRTY);
}

//like replaceScnData except only modifies size amount of data
//starting at offset. If offset+size is longer than the current length
//of the data, extends it as necessary
void modifyScnData(Elf_Data* dataDest,word_t offset,void* data,int size)
{
  if(offset+size > dataDest->d_size)
  {
    dataDest->d_buf=realloc(dataDest->d_buf,offset+size);
    MALLOC_CHECK(dataDest->d_buf);
    //zero out the data we just added that we won't fill
    if(offset > dataDest->d_size)
    {
      memset(dataDest->d_buf+dataDest->d_size,0,offset-dataDest->d_size);
    }
    dataDest->d_size=offset+size;
  }
  memcpy((byte*)dataDest->d_buf+offset,data,size);
  elf_flagdata(dataDest,ELF_C_SET,ELF_F_DIRTY);
}

//adds an entry to the string table, return its offset
int addStrtabEntry(ElfInfo* e, const char* str)
{
  Elf_Data* strtab_data=getDataByERS(e,ERS_STRTAB);
  int len=strlen(str)+1;
  addDataToScn(strtab_data,str,len);
  elf_flagdata(strtab_data,ELF_C_SET,ELF_F_DIRTY);
  return strtab_data->d_size-len;
}

//adds an entry to the section header string table, return its offset
int addShdrStrtabEntry(ElfInfo* e,char* str)
{
  Elf_Scn* scn=elf_getscn(e->e,e->sectionHdrStrTblIdx);
  assert(scn);
  Elf_Data* data=elf_getdata(scn,NULL);
  int len=strlen(str)+1;
  addDataToScn(data,str,len);
  elf_flagdata(data,ELF_C_SET,ELF_F_DIRTY);
  return data->d_size-len;
}

//return index of entry in symbol table
int addSymtabEntry(ElfInfo* e,Elf_Data* data,ElfXX_Sym* sym)
{
  Elf_Data* symtab_data=getDataByERS(e,ERS_SYMTAB);
  int len=sizeof(ElfXX_Sym);
  addDataToScn(data,sym,len);
  if(data==symtab_data)
  {
    e->symTabCount=data->d_off/len;
  }
  return (data->d_size-len)/len;
}

void createSections(ElfInfo* e)
{
  Elf* outelf=e->e;
  e->dataAllocatedByKatana=true;
  //first create the string table
  Elf_Scn* strtab_scn=elf_newscn(outelf);
  Elf_Data* strtab_data=elf_newdata(strtab_scn);
  strtab_data->d_align=1;
  strtab_data->d_buf=NULL;
  strtab_data->d_off=0;
  strtab_data->d_size=0;
  strtab_data->d_version=EV_CURRENT;
  scnInfo[ERS_STRTAB].scn=strtab_scn;
  scnInfo[ERS_STRTAB].data=strtab_data;
  e->sectionIndices[ERS_STRTAB]=elf_ndxscn(strtab_scn);
  
  ElfXX_Shdr* shdr;
  shdr=elfxx_getshdr(strtab_scn);
  shdr->sh_type=SHT_STRTAB;
  shdr->sh_link=SHN_UNDEF;
  shdr->sh_info=SHN_UNDEF;
  shdr->sh_addralign=1;
  shdr->sh_name=1;//first real entry in the string table

  
  
  addStrtabEntry(e,"");//first entry in stringtab null so can have normal unnamed null section
                     //todo: what is the purpose of this?
  addStrtabEntry(e,".strtab");

  //create the data section
  //we use this for holding initializers for new variables or for new fields
  //for existing variables. At present, for simplicity's sake, we have a full
  //entry in here for data objects we are going to transform although in the future
  //it might possible make sense to have a bss section for them so they don't take
  //up too much space in the patch
  Elf_Scn* scn=scnInfo[ERS_DATA].scn=elf_newscn(outelf);

  Elf_Data* data=scnInfo[ERS_DATA].data=elf_newdata(scn);
  data->d_align=1;
  data->d_version=EV_CURRENT;
  shdr=elfxx_getshdr(scn);
  shdr->sh_type=SHT_PROGBITS;
  shdr->sh_link=SHN_UNDEF;
  shdr->sh_info=SHN_UNDEF;
  shdr->sh_addralign=1;  //todo: should this be word-aligned? It seems
                         //that it is in the ELF files I've examined,
                         //but does it have to be?
  shdr->sh_flags=SHF_WRITE;
  shdr->sh_name=addStrtabEntry(e,".data.new");


  //ordinary symtab
  Elf_Scn* symtab_scn=elf_newscn(outelf);
  Elf_Data* symtab_data=elf_newdata(symtab_scn);
  symtab_data->d_align=1;
  symtab_data->d_type=ELF_T_SYM;
  symtab_data->d_buf=NULL;
  symtab_data->d_off=0;
  symtab_data->d_size=0;
  symtab_data->d_version=EV_CURRENT;
  scnInfo[ERS_SYMTAB].scn=symtab_scn;
  scnInfo[ERS_SYMTAB].data=symtab_data;
  e->sectionIndices[ERS_SYMTAB]=elf_ndxscn(symtab_scn);
  
  shdr=elfxx_getshdr(symtab_scn);
  shdr->sh_type=SHT_SYMTAB;
  shdr->sh_link=1;//index of string table
  shdr->sh_info=0;//todo: p.1-13 of ELF format describes this,
                          //but I don't quite understand it
  shdr->sh_addralign=__WORDSIZE;
  shdr->sh_entsize=sizeof(ElfXX_Sym);
  shdr->sh_name=addStrtabEntry(e,".symtab");

  //first symbol in symtab should be all zeros
  ElfXX_Sym sym;
  memset(&sym,0,sizeof(ElfXX_Sym));
  addSymtabEntry(e,symtab_data,&sym);

   //create the section for holding indices to symbols of unsafe
  //functions that can't have activation frames during patching
  scn=scnInfo[ERS_UNSAFE_FUNCTIONS].scn=elf_newscn(outelf);

  data=scnInfo[ERS_UNSAFE_FUNCTIONS].data=elf_newdata(scn);
  data->d_align=sizeof(idx_t);
  data->d_version=EV_CURRENT;
  shdr=elfxx_getshdr(scn);
  shdr->sh_type=SHT_KATANA_UNSAFE_FUNCTIONS;
  shdr->sh_link=elf_ndxscn(symtab_scn);
  shdr->sh_info=SHN_UNDEF;
  shdr->sh_addralign=1;  //todo: should this be word-aligned? It seems
                         //that it is in the ELF files I've examined,
                         //but does it have to be?
  shdr->sh_name=addStrtabEntry(e,".unsafe_functions");

  //text section for new functions
  Elf_Scn* text_scn=elf_newscn(outelf);
  Elf_Data* text_data=elf_newdata(text_scn);
  text_data->d_align=1;
  text_data->d_buf=NULL;
                                       //will be allocced as needed
  text_data->d_off=0;
  text_data->d_size=0;
  text_data->d_version=EV_CURRENT;
  
  shdr=elfxx_getshdr(text_scn);
  shdr->sh_type=SHT_PROGBITS;
  shdr->sh_link=0;
  shdr->sh_info=0;
  shdr->sh_addralign=1;//normally text is aligned, but we never actually execute from this section
  shdr->sh_name=addStrtabEntry(e,".text.new");
  shdr->sh_addr=0;//going to have to relocate anyway so no point in trying to keep the same address
  shdr->sh_flags=SHF_EXECINSTR;
  scnInfo[ERS_TEXT].scn=text_scn;
  scnInfo[ERS_TEXT].data=text_data;

  //rodata section for new strings, constants, etc
  //(note that in many cases these may not actually be "new" ones,
  //but unfortunately because .rodata is so unstructured, it can
  //be difficult to determine what is needed and what is not
  Elf_Scn* rodata_scn=elf_newscn(outelf);
  Elf_Data* rodata_data=elf_newdata(rodata_scn);
  rodata_data->d_align=1;
  rodata_data->d_buf=NULL;
  rodata_data->d_off=0;
  rodata_data->d_size=0;
  rodata_data->d_version=EV_CURRENT;
  scnInfo[ERS_RODATA].scn=rodata_scn;
  scnInfo[ERS_RODATA].data=rodata_data;
  
  shdr=elfxx_getshdr(rodata_scn);
  shdr->sh_type=SHT_PROGBITS;
  shdr->sh_link=0;
  shdr->sh_info=0;
  shdr->sh_addralign=1;//normally text is aligned, but we never actually execute from this section
  shdr->sh_flags=0;
  shdr->sh_name=addStrtabEntry(e,".rodata.new");

  //rela.text.new
  Elf_Scn* rela_text_scn=elf_newscn(outelf);
  Elf_Data* rela_text_data=elf_newdata(rela_text_scn);
  rela_text_data->d_align=1;
  rela_text_data->d_buf=NULL;
  rela_text_data->d_off=0;
  rela_text_data->d_size=0;
  rela_text_data->d_version=EV_CURRENT;
  rela_text_data->d_type=ELF_T_RELA;
  scnInfo[ERS_RELA_TEXT].scn=rela_text_scn;
  scnInfo[ERS_RELA_TEXT].data=rela_text_data;
  shdr=elfxx_getshdr(rela_text_scn);
  shdr->sh_type=SHT_RELA;
  shdr->sh_addralign=__WORDSIZE;
  shdr->sh_name=addStrtabEntry(e,".rela.text.new");

  //write symbols for sections
  sym.st_info=ELFXX_ST_INFO(STB_LOCAL,STT_SECTION);
  for(int i=0;i<ERS_CNT;i++)
  {
    if(scnInfo[i].scn)
    {
      sym.st_name=elfxx_getshdr(scnInfo[i].scn)->sh_name;
      sym.st_shndx=elf_ndxscn(scnInfo[i].scn);
      addSymtabEntry(e,symtab_data,&sym);
    }
  }


  //fill in some info about the sections we added
  e->sectionIndices[ERS_TEXT]=elf_ndxscn(text_scn);
  e->sectionIndices[ERS_RODATA]=elf_ndxscn(rodata_scn);
  e->sectionIndices[ERS_RELA_TEXT]=elf_ndxscn(rela_text_scn);
  e->sectionIndices[ERS_DATA]=elf_ndxscn(scnInfo[ERS_DATA].scn);
  e->sectionIndices[ERS_UNSAFE_FUNCTIONS]=elf_ndxscn(scnInfo[ERS_UNSAFE_FUNCTIONS].scn);
}

//Must be called before any other routines for each patch object to
//create. Filename is just used for identification purposes it is not
//enforced to correspond to file. If file is NULL, however, a new file
//will be created at filename
ElfInfo* startPatchElf(FILE* file,char* filename)
{
  if(!file)
  {
    file=fopen(filename,"w");
  }
  patch=zmalloc(sizeof(ElfInfo));
  patch->isPO=true;
  patch->fname=strdup(filename);
  int outfd = fileno(file);

  patch->e = elf_begin (outfd, ELF_C_WRITE, NULL);
  //todo: get rid of the need for permissive. Need it right now
  //because libdwarf creates some sections with wrong entsizes.
  elf_flagelf(patch->e,ELF_C_SET,ELF_F_PERMISSIVE);
  ElfXX_Ehdr* ehdr=elfxx_newehdr(patch->e);
  if(!ehdr)
  {
    death("Unable to create new ehdr\n");
  }
  ehdr->e_ident[EI_MAG0]=ELFMAG0;
  ehdr->e_ident[EI_MAG1]=ELFMAG1;
  ehdr->e_ident[EI_MAG2]=ELFMAG2;
  ehdr->e_ident[EI_MAG3]=ELFMAG3;
  ehdr->e_ident[EI_CLASS]=ELFCLASSXX;
  ehdr->e_ident[EI_DATA]=ELFDATA2LSB;
  ehdr->e_ident[EI_VERSION]=EV_CURRENT;
  ehdr->e_ident[EI_OSABI]=ELFOSABI_LINUX;//todo: support systems other than Linux
#ifdef KATANA_X86_ARCH
  ehdr->e_machine=EM_386;
#elif defined(KATANA_X86_64_ARCH)
  ehdr->e_machine=EM_X86_64;
#else
#error Unknown architecture
#endif
  
  ehdr->e_type=ET_NONE;//not relocatable, or executable, or shared object, or core, etc
  ehdr->e_version=EV_CURRENT;

  createSections(patch);
  ehdr->e_shstrndx=elf_ndxscn(getSectionByERS(patch,ERS_STRTAB));//set strtab in elf header
  //todo: perhaps the two string tables should be separate. ELF
  //normally does this, but using only the one has worked ok for me so
  //far. I should look into it.
  patch->strTblIdx=patch->sectionHdrStrTblIdx=ehdr->e_shstrndx;
  return patch;
}

void finalizeDataSize(ElfInfo* e,Elf_Scn* scn,Elf_Data* data)
{
  ElfXX_Shdr* shdr=elfxx_getshdr(scn);
  shdr->sh_size=data->d_size;
  elf_flagshdr(scn,ELF_C_SET,ELF_F_DIRTY);
  if(shdr->sh_type!=SHT_NOBITS)
  {
    elf_flagdata(data,ELF_C_SET,ELF_F_DIRTY);
  }
  logprintf(ELL_INFO_V3,ELS_ELFWRITE,"finalizing data size to 0x%x for section with name %s(%i)\n",shdr->sh_size,getScnHdrString(e,shdr->sh_name),shdr->sh_name);
}



void finalizeDataSizes(ElfInfo* e)
{

  for(Elf_Scn* scn=elf_nextscn(e->e,NULL);scn;scn=elf_nextscn(e->e,scn))
  {
    Elf_Data* data=elf_getdata(scn,NULL);
    finalizeDataSize(e,scn,data);
  }
  //todo: do a second pass and make all the layouts come out right.
  ElfXX_Shdr* lastShdr=NULL;
  for(Elf_Scn* scn=elf_nextscn(e->e,NULL);scn;scn=elf_nextscn(e->e,scn))
  {
    ElfXX_Shdr* shdr=elfxx_getshdr(scn);
    if((shdr->sh_flags & SHF_ALLOC) && lastShdr && (lastShdr->sh_flags & SHF_ALLOC))
    {
      //note that this test is not foolproof, we're assuming that two
      //overlapping sections are listed one after the other
      int overlap=(lastShdr->sh_addr+lastShdr->sh_size)-shdr->sh_addr;
      if(overlap>1)
      {
        death("When loaded into memory, sections '%s' and '%s' (at addressex 0x%x and 0x%x respectively) in ELF file %s overlap by %i bytes, presumably from the first section expanding to %#zx bytes. Katana should be capable of resizing appropriately (although with some difficulty when relocations are involved and the binary has not been linked with --emit-relocs) but this feature has not yet been implemented\n",getScnHdrString(e,lastShdr->sh_name),getScnHdrString(e,shdr->sh_name),
              lastShdr->sh_addr,shdr->sh_addr,e->fname,overlap,lastShdr->sh_size);
      }
    }
    if(shdr->sh_flags & SHF_ALLOC)
    {
      lastShdr=shdr;
    }
  }  
}

//prepare a modified elf object for writing
void finalizeModifiedElf(ElfInfo* e)
{
  finalizeDataSizes(e);
}



/*
  //The code commented out below was in the removed function
  //endPatchElf but commented out at the time that function was
  //removed. I haven't yet thought about whether this snippet of code
  //will ever be useful
  
  //all symbols created so far that were relative to sections
  //assumed that their sections started at location 0. This obviously
  //can't be true of all sections. We now relocate the symbols appropriately
  int numEntries=symtab_data->d_size/sizeof(ElfXX_Sym);
  for(int i=1;i<numEntries;i++)
  {
    ElfXX_Sym* sym=symtab_data->d_buf+i*sizeof(ElfXX_Sym);
    if(sym->st_shndx!=SHN_UNDEF && sym->st_shndx!=SHN_ABS && sym->st_shndx!=SHN_COMMON)
    {
      //symbol needs rebasing
      Elf_Scn* scn=elf_getscn(outelf,sym->st_shndx);
      assert(scn);
      GElf_Shdr shdr;
      if(!gelf_getshdr(scn,&shdr))
      {death("gelf_getshdr failed\n");}
      sym->st_value+=shdr.sh_addr;
    }
    }*/


int reindexSectionForPatch(ElfInfo* e,int scnIdx,ElfInfo* patch)
{
  char* scnName=getSectionNameFromIdx(e,scnIdx);
  if(!strcmp(".rodata",scnName))
  {
    return elf_ndxscn(getSectionByERS(patch,ERS_RODATA));
  }
  else
  {
    //perhaps we have a section with that name
    Elf_Scn* patchScn=getSectionByName(patch,scnName);
    if(!patchScn)
    {
      //ok, we have to create a section with that name
      //just so we can refer to it with a symbol
      //so that it can get reindexed to the appropriate section
      //in the original binary when we apply the patch
      Elf_Scn* scn=elf_getscn(e->e,scnIdx);
      GElf_Shdr shdr;
      getShdr(scn,&shdr);
      patchScn=elf_newscn(patch->e);
      elf_newdata(patchScn);//libelf seems to get pissy if a section doesn't have any data, I'm not quite sure why
      ElfXX_Shdr* patchShdr=elfxx_getshdr(patchScn);
      patchShdr->sh_type=shdr.sh_type;
      patchShdr->sh_link=shdr.sh_link;
      patchShdr->sh_info=shdr.sh_info;
      patchShdr->sh_addralign=shdr.sh_addralign;
      patchShdr->sh_name=addStrtabEntry(patch,scnName);
      //need to add a symbol for this section
      ElfXX_Sym sym;
      memset(&sym,0,sizeof(ElfXX_Sym));
      sym.st_info=ELFXX_ST_INFO(STB_LOCAL,STT_SECTION);
      sym.st_name=patchShdr->sh_name;
      sym.st_shndx=elf_ndxscn(patchScn);
      addSymtabEntry(patch,getDataByERS(patch,ERS_SYMTAB),&sym);
      
    }
    return elf_ndxscn(patchScn);
  }
  return STN_UNDEF;
}

int dwarfWriteSectionCallback(const char* name,int size,Dwarf_Unsigned type,
                              Dwarf_Unsigned flags,Dwarf_Unsigned link,
                              Dwarf_Unsigned info,Dwarf_Unsigned* sectNameIdx,
                              void* user_data, int* error)
{
  ElfInfo* e=patch;
  //look through all the sections for one which matches to
  //see if we've already created this section
  Elf_Scn* scn=elf_nextscn(e->e,NULL);
  int nameLen=strlen(name);
  Elf_Data* symtab_data=getDataByERS(e,ERS_SYMTAB);
  Elf_Data* strtab_data=getDataByERS(e,ERS_STRTAB);
  for(;scn;scn=elf_nextscn(e->e,scn))
  {
    ElfXX_Shdr* shdr=elfxx_getshdr(scn);
    if(!strncmp(strtab_data->d_buf+shdr->sh_name,name,nameLen))
    {
      //ok, we found the section we want, now have to find its symbol
      int symtabSize=symtab_data->d_size;
      for(int i=0;i<symtabSize/sizeof(ElfXX_Sym);i++)
      {
        ElfXX_Sym* sym=(ElfXX_Sym*)(symtab_data->d_buf+i*sizeof(ElfXX_Sym));
        int idx=elf_ndxscn(scn);
        //printf("we're on section with index %i and symbol for index %i\n",idx,sym->st_shndx);
        if(STT_SECTION==ELFXX_ST_TYPE(sym->st_info) && idx==sym->st_shndx)
        {
          *sectNameIdx=i;
          return idx;
        }
      }
      fprintf(stderr,"finding existing section for %s\n",name);
      death("found section already existing but had no symbol, this should be impossible\n");
    }
  }

  //section doesn't already exist, create it
 
  //todo: write this section
  scn=elf_newscn(e->e);
  ElfXX_Shdr* shdr=elfxx_getshdr(scn);
  shdr->sh_name=addStrtabEntry(e,name);
  shdr->sh_type=type;
  shdr->sh_flags=flags;
  shdr->sh_size=size;
  shdr->sh_entsize=1; //todo: find some way to actually set this intelligently
  //printf("creating new dwarf section %s with name at %d and size %zd\n",name,shdr->sh_name,shdr->sh_size);
  shdr->sh_link=link;
  if(0==link && SHT_REL==type)
  {
    //make symtab the link
    shdr->sh_link=elf_ndxscn(getSectionByERS(e,ERS_SYMTAB));
  }
  shdr->sh_info=info;
  ElfXX_Sym sym;
  sym.st_name=shdr->sh_name;
  sym.st_value=0;//don't yet know where this symbol will end up. todo: fix this, so relocations can theoretically be done
  sym.st_size=0;
  sym.st_info=ELFXX_ST_INFO(STB_LOCAL,STT_SECTION);
  sym.st_other=0;
  sym.st_shndx=elf_ndxscn(scn);
  if(!strcmp(".debug_info",name))
  {
    e->sectionIndices[ERS_DEBUG_INFO]=elf_ndxscn(scn);
  }
  *sectNameIdx=addSymtabEntry(patch,symtab_data,&sym);
  *error=0;
  return sym.st_shndx;
}
