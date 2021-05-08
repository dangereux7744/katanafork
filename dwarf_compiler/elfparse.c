/*
  File: elfparse.c
  Author: James Oakley
  Copyright (C): 2010 Dartmouth College
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
  Date: January 10
  Description: Read information from an ELF file
*/

#include "elfparse.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "util/logging.h"
#include "fderead.h"
#include "symbol.h"
//#include "../config.h"

//the ELF file is always opened read-only. If you want to write a copy
//to disk, call writeOutElf. 
ElfInfo* openELFFile(char* fname)
{
  ElfInfo* e=zmalloc(sizeof(ElfInfo));
  e->fname=strdup(fname);
  e->fd=open(fname,O_RDONLY);
  if(e->fd < 0)
  {
    logprintf(ELL_WARN,ELS_MISC,"Failed to open elf file %s (open returned invalid fd)\n",fname);
    return NULL;

  }
  e->e=elf_begin(e->fd,ELF_C_READ,NULL);
  if(!e->e)
  {
    logprintf(ELL_WARN,ELS_MISC,"Failed to open file %s as an ELF file %s\n",fname,elf_errmsg(-1));
    free(e);
    return NULL;
  }
  size_t nbytes;
  char* identBytes=elf_getident(e->e,&nbytes);
  switch(identBytes[EI_CLASS])
  {
  case ELFCLASS32:
    #ifdef KATANA_X86_64_ARCH
    logprintf(ELL_WARN,ELS_MISC,"This is a 64-bit version of katana but you are trying to work with a 32-bit ELF file. No testing or attempt to ensure correctness has been made for this case. You will probably do better compiling a 32-bit version of katana. If this feature is important to you, please send email tosomepackagebugreport\n");
    #endif
    break;
  case ELFCLASS64:
#ifdef KATANA_X86_ARCH
    logprintf(ELL_WARN,ELS_MISC,"This is a 32-bit version of katana but you are trying to work with a 64-bit ELF file. No testing or attempt to ensure correctness has been made for this case. You will probably do better compiling a 32-bit version of katana. If this feature is important to you, please send email tosomepackagebugreport\n");
#endif
    break;
  default:
    logprintf(ELL_WARN,ELS_MISC,"Unrecognized ELF class, results may not be what you want");
  }
  findELFSections(e);
  return e;
}

void endELF(ElfInfo* e)
{
  logprintf(ELL_INFO_V2,ELS_CLEANUP,"ending elf %s\n",e->fname);
  if(e->dataAllocatedByKatana)
  {
    //since we wrote this elf file we malloc'd all the
    //data sections, and therefore libelf won't free them,
    //so we have to do it ourselves
    for(Elf_Scn* scn=elf_nextscn (e->e,NULL);scn;scn=elf_nextscn(e->e,scn))
    {
      Elf_Data* data=elf_getdata(scn,NULL);
      if(data && data->d_buf)
      {
        free(data->d_buf);
      }
    }
  }
  if(e->dwarfInfo)
  {
    freeDwarfInfo(e->dwarfInfo);
  }
  //todo: this is not correct and leaks, need to a proper destroy function
  for(int i=0;i<e->callFrameInfo.numFDEs;i++)
  {
    free(e->callFrameInfo.fdes[i].instructions);
  }
  free(e->callFrameInfo.fdes);
  elf_end(e->e);
  //I think elf_end must call close on the file descriptor
  //close(e->fd);
  free(e->fname);
  free(e);
}


//have to pass the name that the elf file will originally get written
//out to, because of the way elf_begin is set up
ElfInfo* duplicateElf(ElfInfo* e,char* outfname,bool flushToDisk,bool keepLayout)
{
  int outfd = open(outfname, O_WRONLY|O_CREAT,S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH);
  if (outfd < 0)
  {
    logprintf(ELL_WARN,ELS_ELFWRITE,"cannot open output file '%s' for writing\n", outfname);
    perror("Error code is: ");
    return NULL;
  }
  //code inspired by ecp in elfutils tests
  Elf *outelf = elf_begin(outfd, ELF_C_WRITE, NULL);

  gelf_newehdr (outelf, gelf_getclass(e->e));
  GElf_Ehdr ehdr;
  if(!gelf_getehdr(e->e,&ehdr))
  {
    fprintf(stdout,"Failed to get ehdr from elf file we're duplicating: %s\n",elf_errmsg (-1));
    death(NULL);
  }
  gelf_update_ehdr(outelf,&ehdr);
  if(ehdr.e_phnum > 0)
  {
    int cnt;
    gelf_newphdr(outelf,ehdr.e_phnum);
    for (cnt = 0; cnt < ehdr.e_phnum; ++cnt)
    {
      GElf_Phdr phdr_mem;
      if(!gelf_getphdr(e->e,cnt,&phdr_mem))
      {
        fprintf(stdout,"Failed to get phdr from elf file we're duplicating: %s\n",elf_errmsg (-1));
        death(NULL);
      }
      gelf_update_phdr(outelf,cnt,&phdr_mem);
    }
  }
  Elf_Scn* scn=NULL;
  while ((scn = elf_nextscn(e->e,scn)))
  {
    Elf_Scn *newscn = elf_newscn (outelf);
    GElf_Shdr shdr;
    gelf_update_shdr(newscn,gelf_getshdr(scn,&shdr));
    Elf_Data* newdata=elf_newdata(newscn);
    Elf_Data* data=elf_getdata (scn,NULL);
    assert(data);
    newdata->d_off=data->d_off;
    newdata->d_size=data->d_size;
    newdata->d_align=data->d_align;
    newdata->d_version=data->d_version;
    newdata->d_type=data->d_type;
    if(SHT_NOBITS!=shdr.sh_type)
    {
      newdata->d_buf=zmalloc(newdata->d_size);
      memcpy(newdata->d_buf,data->d_buf,data->d_size);
    }
  }


  elf_flagelf(outelf, ELF_C_SET, ELF_F_DIRTY);
  if(keepLayout)
  {
    elf_flagelf(outelf,ELF_C_SET,ELF_F_LAYOUT);
  }

  if (elf_update(outelf, flushToDisk?ELF_C_WRITE:ELF_C_NULL) <0)
  {
    fprintf(stdout,"Failed to write out elf file: %s\n",elf_errmsg (-1));
    return NULL;
  }
  ElfInfo* newE=zmalloc(sizeof(ElfInfo));
  newE->dataAllocatedByKatana=true;
  newE->fd=outfd;
  newE->fname=strdup(outfname);
  newE->e=outelf;
  findELFSections(newE);
  return newE;
}

//return true on success
bool writeOutElf(ElfInfo* e,char* outfname,bool keepLayout)
{
  ElfInfo* newE=duplicateElf(e,outfname,true,keepLayout);
  if(newE)
  {
    elf_end(newE->e);
    //elf_end seems to close the file descriptor
    //close(newE->fd);
    free(newE);
    return true;
  }
  return false;
}

void findELFSections(ElfInfo* e)
{
  elf_getshdrstrndx(e->e, &e->sectionHdrStrTblIdx);

  memset(e->sectionIndices,0,sizeof(int)*ERS_CNT);
  for(Elf_Scn* scn=elf_nextscn (e->e,NULL);scn;scn=elf_nextscn(e->e,scn))
  {
    GElf_Shdr shdr;
    gelf_getshdr(scn,&shdr);
    char* name=elf_strptr(e->e,e->sectionHdrStrTblIdx,shdr.sh_name);
    if(!strcmp(".hash",name))
    {
      logprintf(ELL_INFO_V4,ELS_MISC,"found symbol hash table\n");
      e->sectionIndices[ERS_HASHTABLE]=elf_ndxscn(scn);
    }
    else if(!strcmp(".symtab",name))
    {
      e->sectionIndices[ERS_SYMTAB]=elf_ndxscn(scn);
      e->symTabCount = shdr.sh_size / shdr.sh_entsize;
      e->strTblIdx=shdr.sh_link;
    }
    else if(!strcmp(".strtab",name))
    {
      e->sectionIndices[ERS_STRTAB]=elf_ndxscn(scn);
      //todo: use this or get sh_link value from .symtab section
      // strTblIdx=elf_ndxscn(scn);
    }
    else if(!strcmp(".rel.text",name))
    {
      e->textRelocData=elf_getdata(scn,NULL);
      e->textRelocCount = shdr.sh_size / shdr.sh_entsize;
    }
    else if(!strncmp(".data",name,strlen(".data")))//allow versioned data sections in patches as well as text
    {
      e->sectionIndices[ERS_DATA]=elf_ndxscn(scn);
      e->dataStart[IN_MEM]=shdr.sh_addr;
      e->dataStart[ON_DISK]=shdr.sh_offset;
      //printf("data size is 0x%x at offset 0x%x\n",dataData->d_size,(uint)dataData->d_off);
      //printf("data section starts at address 0x%x\n",dataStart);

      //assuming that if we have a .data section at all, then
      //using small code model. If using only large code model,
      //would only have a .ldata section
      #ifdef KATANA_X86_64_ARCH
      e->textUsesSmallCodeModel=true;
      #endif
    }
    else if(!strncmp(".text",name,strlen(".text"))) //allow versioned text sections in patches as well as .text
    {
      if(!strcmp(".text.new",name))
      {
        //If there's a .text.new section then we assume it's a patch object
        e->isPO=true;
      }
      e->sectionIndices[ERS_TEXT]=elf_ndxscn(scn);
      e->textStart[IN_MEM]=shdr.sh_addr;
      e->textStart[ON_DISK]=shdr.sh_offset;
      //assuming that if we have a .text section at all, then
      //using small code model. If using only large code model,
      //would only have a .ltext section
      #ifdef KATANA_X86_64_ARCH
      e->textUsesSmallCodeModel=true;
      #endif

    }
    else if(!strncmp(".rodata",name,strlen(".rodata"))) //allow versioned
                         //sections in patches
    {
      e->sectionIndices[ERS_RODATA]=elf_ndxscn(scn);
      //assuming that if we have a .rodata section at all, then
      //using small code model. If using only large code model,
      //would only have a .lrodata section
      #ifdef KATANA_X86_64_ARCH
      e->textUsesSmallCodeModel=true;
      #endif

    }
    else if(!strncmp(".rela.text",name,strlen(".rela.text"))) //allow versioned
                         //sections in patches
    {
      e->sectionIndices[ERS_RELA_TEXT]=elf_ndxscn(scn);
    }
    else if(!strncmp(".rel.text",name,strlen(".rel.text"))) //allow versioned
                         //sections in patches. //todo: problem with this. Also allows section-specific ones, probably don't want this
    {
      e->sectionIndices[ERS_REL_TEXT]=elf_ndxscn(scn);
    }
    else if(!strcmp(".got",name))
    {
      e->sectionIndices[ERS_GOT]=elf_ndxscn(scn);
    }
    else if(!strcmp(".got.plt",name))
    {
      e->sectionIndices[ERS_GOTPLT]=elf_ndxscn(scn);
    }
    else if(!strcmp(".plt",name))
    {
      e->sectionIndices[ERS_PLT]=elf_ndxscn(scn);
    }
    else if(!strcmp(".rel.plt",name))
    {
      assert(!e->sectionIndices[ERS_RELX_PLT]);
      e->sectionIndices[ERS_RELX_PLT]=elf_ndxscn(scn);
    }
    else if(!strcmp(".rela.plt",name))
    {
      assert(!e->sectionIndices[ERS_RELX_PLT]);
      e->sectionIndices[ERS_RELX_PLT]=elf_ndxscn(scn);
    }
    else if(!strcmp(".dynsym",name))
    {
      e->sectionIndices[ERS_DYNSYM]=elf_ndxscn(scn);
    }
    else if(!strcmp(".dynstr",name))
    {
      e->sectionIndices[ERS_DYNSTR]=elf_ndxscn(scn);
    }
    else if(!strcmp(".dynamic",name))
    {
      e->sectionIndices[ERS_DYNAMIC]=elf_ndxscn(scn);
    }
    else if(!strcmp(".unsafe_functions",name))
    {
      e->sectionIndices[ERS_UNSAFE_FUNCTIONS]=elf_ndxscn(scn);
    }
    else if(!strcmp(".debug_info",name))
    {
      e->sectionIndices[ERS_DEBUG_INFO]=elf_ndxscn(scn);
    }
    else if(!strcmp(".eh_frame",name))
    {
      e->sectionIndices[ERS_EH_FRAME]=elf_ndxscn(scn);
    }
  }
  //todo: support x86_64 sections ltext, ldata,lrdodata, etc
}
