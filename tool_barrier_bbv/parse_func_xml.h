#ifndef _PARSE_FUNC_XML_H_
#define _PARSE_FUNC_XML_H_

#include <string>

struct funcData
{
   uint64_t main;
   uint64_t exit;
   uint64_t GOMP_parallel_start;
};

funcData parseFuncData(std::string fn);

#endif /* _PARSE_FUNC_XML_H_ */
