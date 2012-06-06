/*******************************************************************\

Module: C++ Language Type Checking

Author: Daniel Kroening, kroening@cs.cmu.edu

\*******************************************************************/

#include "cpp_exception_id.h"

/*******************************************************************\

Function: cpp_exception_list_rec

  Inputs:

 Outputs:

 Purpose: turns a type into a list of relevant exception IDs

\*******************************************************************/

#include <iostream>

void cpp_exception_list_rec(
  const typet &src,
  const namespacet &ns,
  const std::string &suffix,
  std::vector<irep_idt> &dest)
{
  if(src.id()=="pointer")
  {
    if(src.reference())
    {
      // do not change
      cpp_exception_list_rec(src.subtype(), ns, suffix, dest);
      return;
    }
    else
    {
      // append suffix _ptr
      cpp_exception_list_rec(src.subtype(), ns, "_ptr"+suffix, dest);
      return;
    }
  }
  else if(src.id()=="symbol")
  {
    irep_idt identifier = src.identifier();
    dest.push_back(id2string(identifier)+suffix);
  }

  // grab C++ type
  irep_idt cpp_type=src.get("#cpp_type");

  if(cpp_type!=irep_idt())
  {
    dest.push_back(id2string(cpp_type)+suffix);
    return;
  }

  return;
}

/*******************************************************************\

Function: cpp_exception_list

  Inputs:

 Outputs:

 Purpose: turns a type into a list of relevant exception IDs

\*******************************************************************/

irept cpp_exception_list(const typet &src, const namespacet &ns)
{
  std::vector<irep_idt> ids;
  irept result("exception_list");

  cpp_exception_list_rec(src, ns, "", ids);
  result.get_sub().resize(ids.size());

  for(unsigned i=0; i<ids.size(); i++)
    result.get_sub()[i].id(ids[i]);

  return result;
}

/*******************************************************************\

Function: cpp_exception_id

  Inputs:

 Outputs:

 Purpose: turns a type into an exception ID

\*******************************************************************/

irep_idt cpp_exception_id(const typet &src, const namespacet &ns)
{
  std::vector<irep_idt> ids;
  cpp_exception_list_rec(src, ns, "", ids);
  assert(!ids.empty());
  return ids.front();
}
