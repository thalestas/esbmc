/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_REPLACE_SYMBOL_H
#define CPROVER_REPLACE_SYMBOL_H

//
// true: did nothing
// false: replaced something
//

#include <util/expr.h>
#include <util/hash_cont.h>

class replace_symbolt
{
 public:
  typedef hash_map_cont<irep_idt, exprt> expr_mapt;
  typedef hash_map_cont<irep_idt, typet> type_mapt;
  
  void insert(const irep_idt &identifier,
              const exprt &expr)
  {
    expr_map.insert(std::pair<irep_idt, exprt>(identifier, expr));
  }
  
  void insert(const irep_idt &identifier,
              const typet &type)
  {
    type_map.insert(std::pair<irep_idt, typet>(identifier, type));
  }
  
  virtual bool replace(exprt &dest);
  virtual bool replace(typet &dest);

  replace_symbolt() = default;
  virtual ~replace_symbolt() = default;
  
 protected:
  expr_mapt expr_map;
  type_mapt type_map;
};

#endif
