#include "parse_func_xml.h"

#include <string>
#include <iostream>
#include <fstream>
#include <streambuf>

#include "rapidxml/rapidxml.hpp"

uint64_t parseBase(rapidxml::xml_node<> *symbol)
{
   uint64_t r = 0;
   rapidxml::xml_attribute<> *base = symbol->first_attribute("base");

   if (!base) {
      return 0;
   }

   errno = 0;
   r = strtoll(base->value(), NULL, 0);

   if (errno != 0)
   {
      return 0;
   }

   return r;
}

// Info from http://stackoverflow.com/questions/2602013/read-whole-ascii-file-into-c-stdstring
funcData parseFuncData(std::string fn)
{
   funcData f = {0};

   // First, grab all of the file data
   std::ifstream t(fn.c_str());
   std::string str;

   if (!t.is_open())
   {
      std::cerr << "Unable to parse the program XML data in file [" << fn << "]" << std::endl;
      exit(1);
   }

   t.seekg(0, std::ios::end);   
   //std::cerr << "file size = " << t.tellg() << std::endl;
   str.reserve(t.tellg());
   t.seekg(0, std::ios::beg);

   str.assign((std::istreambuf_iterator<char>(t)),
      std::istreambuf_iterator<char>());

   // Parse the XML data
   rapidxml::xml_document<> doc;
   doc.parse<rapidxml::parse_non_destructive>(const_cast<char*>(str.c_str()));

   rapidxml::xml_node<> *procinfo = doc.first_node("proc-info");
   if (procinfo == NULL)
   {
      std::cerr << "Unable to find node procinfo" << std::endl;
      return f;
   }
   rapidxml::xml_node<> *process = procinfo->first_node("process");
   if (process == NULL)
   {
      std::cerr << "Unable to find node process" << std::endl;
      return f;
   }

   for (rapidxml::xml_node<> *module = process->first_node("module") ; module ; module = module->next_sibling("module"))
   {
      for (rapidxml::xml_node<> *symbol = module->first_node("symbol") ; symbol ; symbol = symbol->next_sibling("symbol"))
      {
         rapidxml::xml_attribute<> *name = symbol->first_attribute("name");
         if (!name)
         {
            continue;
         }
         std::string val(name->value(), 0, name->value_size());

         if (val == "main")
         {
            f.main = parseBase(symbol);
         }
         else if (val == "exit")
         {
            f.exit = parseBase(symbol);
         }
         else if (val == "GOMP_parallel_start")
         {
            f.GOMP_parallel_start = parseBase(symbol);
         }
      }
   }

   //std::cout << std::hex << "main: " << f.main << " exit: " << f.exit << " GOMP: " << f.GOMP_parallel_start << std::dec << std::endl;

   return f;
}
