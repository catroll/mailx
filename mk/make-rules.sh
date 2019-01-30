#!/bin/sh -
#@ make-rules.sh: create make rules for the given .cc? files.
#@ All given files must reside in a single directory.
#@ Comments following '#include "file"' directives will replace the default
#@ $(DIRNAME_INCDIR) prefix; use - to not add a dependency for that header.
# Public Domain

: ${awk:=awk}
: ${sort:=sort}

# Whether we shall generate object names based on numbers (shorter)?
: ${COUNT_MODE:=1}

##  --  >8  - -  8<  --  ##

syno() {
   if [ ${#} -gt 0 ]; then
      echo >&2 "ERROR: ${*}"
      echo >&2
   fi
   echo >&2 'Synopsis: make-rules.sh :FILES:'
   exit 1
}

[ ${#} -gt 0 ] || syno

APO=\' #' (vimsyn)
printf '%s\n' "${@}" | ${sort} | ${awk} -v COUNT_MODE=${COUNT_MODE} '
   BEGIN {fno = 0; cono = cxxono = 0; dname = ""; DNAME = ""}
   {farr[++fno] = $0}
   END{
      for(i = 1; i <= fno; ++i)
         parse_one(i)

      if(cono > 0){
         printf DNAME "_C_OBJ ="
         for(i = 1; i <= cono; ++i)
            printf " " coarr[i]
         print ""
      }

      if(cxxono > 0){
         printf DNAME "_CXX_OBJ ="
         for(i = 1; i <= cxxono; ++i)
            printf " " cxxoarr[i]
         print ""
      }
   }

   function parse_one(no){
      # The source file
      po_i = "basename " farr[no]
      po_j = po_i | getline bname
      close(po_i)
      if(po_j == -1)
         exit(1)

      # On first invocation, create our dirname prefixes
      if(no == 1){
         po_i = "dirname " farr[no]
         po_j = po_i | getline dname
         close(po_i)
         if(po_j == -1)
            exit(1)

         DNAME = dname
         dname = tolower(dname)
         gsub("/", "-", dname)
         DNAME = toupper(DNAME)
         gsub("/", "_", DNAME)
         gsub("-", "_", DNAME)
      }

      # Classify file type
      po_i = bname
      if(po_i ~ /\.c$/){
         sub(".c$", "", po_i)
         is_c = 1
         ++cono
      }else if(po_i ~ /\.cc$/){
         sub(".cc$", "", po_i)
         is_c = 0
         ++cxxono
      }else{
         print "ERROR: not a C or C++ file: " bname
         exit(2)
      }

      # Object file name
      po_i = COUNT_MODE ? dname "-" sprintf("%03u", cono + cxxono) ".o" \
            : dname "-" po_i ".o"
      if(is_c)
         coarr[cono] = po_i
      else
         cxxoarr[cxxono] = po_i
      po_i = po_i ": $(" DNAME "_SRCDIR)" dname "/" bname
      printf po_i

      # Parse the file and generate dependencies
      for(any = 0;;){
         po_i = getline < farr[no]
         if(po_i == 0)
            break
         if(po_i == -1)
            exit(1)

         po_i = match($0, /^[[:space:]]*#[[:space:]]*include "/)
         if(po_i == 0)
            continue

         h = substr($0, RSTART + RLENGTH)
         po_i = match(h, /"/)
         xh = substr(h, 1, RSTART - 1)

         # Do we have a comment suffix that clarifies our path?
         h = substr(h, RSTART + 1)
         po_i = match(h, /[[:space:]]*\/\*[[:space:]]*[^[:space:]]/)
         if(po_i == 0){
            if(!any++)
               printf " \\\n\t\t"
            printf " $(" DNAME "_INCDIR)" xh
         }else{
            h = substr(h, RSTART + RLENGTH - 1) # include first non-space
            po_i = match(h, /[[:space:]]|\*\//)
            h = substr(h, 1, RSTART - 1)
            # No dependency for this?
            if(h != "-"){
               if(!any++)
                  printf " \\\n\t\t"
               printf " " h xh
            }
         }
      }
      printf "\n\t$(ECHO_CC)$("
      if(is_c)
         printf "CC) $(" DNAME "_CFLAGS)"
      else
         printf "CXX) $(" DNAME "_CXXFLAGS)"
      print " $(" DNAME "_INCS) -c -o $(@) $(" DNAME "_SRCDIR)" dname "/" bname
   }
'

# s-sh-mode