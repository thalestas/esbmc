/*******************************************************************\

   Module:

   Author: Lucas Cordeiro, lcc08r@ecs.soton.ac.uk

\*******************************************************************/

#include "execution_state.h"
#include <string>
#include <sstream>
#include <vector>
#include <i2string.h>
#include <string2array.h>
#include <std_expr.h>
#include <expr_util.h>
#include "../ansi-c/c_types.h"
#include <base_type.h>
#include <simplify_expr.h>
#include "config.h"

unsigned int execution_statet::node_count = 0;

execution_statet::execution_statet(const goto_functionst &goto_functions,
                                   const namespacet &ns,
                                   const reachability_treet *art,
                                   symex_targett *_target,
                                   goto_symex_statet::level2t &l2,
                                   contextt &context,
                                   const optionst &options,
                                   bool _is_schedule) :
  goto_symext(ns, context, _target, options),
  owning_rt(art),
  state_level2(l2),
  _goto_functions(goto_functions)
{

  // XXXjmorse - C++s static initialization order trainwreck means
  // we can't initialize the id -> serializer map statically. Instead,
  // manually inspect and initialize. This is not thread safe.
  if (!execution_statet::expr_id_map_initialized) {
    execution_statet::expr_id_map_initialized = true;
    execution_statet::expr_id_map = init_expr_id_map();
  }

  is_schedule = _is_schedule;
  reexecute_instruction = true;
  CS_number = 0;
  TS_number = 0;
  node_id = 0;
  guard_execution = "execution_statet::\\guard_exec";
  guard_thread = "execution_statet::\\trdsel";

  goto_functionst::function_mapt::const_iterator it =
    goto_functions.function_map.find("main");
  if (it == goto_functions.function_map.end())
    throw "main symbol not found; please set an entry point";

  add_thread(
    it->second.body.instructions.begin(),
    it->second.body.instructions.end(), &(it->second.body));
  active_thread = 0;
  last_active_thread = 0;
  generating_new_threads = 0;
  node_count = 0;
  nondet_count = 0;
  dynamic_counter = 0;
  DFS_traversed.reserve(1);
  DFS_traversed[0] = false;

  str_state = string_container.take_state_snapshot();
}

execution_statet::execution_statet(const execution_statet &ex) :
  goto_symext(ex),
  owning_rt(ex.owning_rt),
  state_level2(ex.state_level2),
  _goto_functions(ex._goto_functions)
{
  *this = ex;

  // Don't copy string state in this copy constructor - instead
  // take another snapshot to represent what string state was
  // like when we began the exploration this execution_statet will
  // perform.
  str_state = string_container.take_state_snapshot();

  // Regenerate threads state using new objects state_level2 ref
  threads_state.clear();
  std::vector<goto_symex_statet>::const_iterator it;
  for (it = ex.threads_state.begin(); it != ex.threads_state.end(); it++) {
    goto_symex_statet state(*it, state_level2);
    threads_state.push_back(state);
  }
}

execution_statet&
execution_statet::operator=(const execution_statet &ex)
{
  is_schedule = ex.is_schedule;
  threads_state = ex.threads_state;
  atomic_numbers = ex.atomic_numbers;
  DFS_traversed = ex.DFS_traversed;
  generating_new_threads = ex.generating_new_threads;
  exprs_read_write = ex.exprs_read_write;
  last_global_read_write = ex.last_global_read_write;
  last_active_thread = ex.last_active_thread;
  state_level2 = ex.state_level2;
  active_thread = ex.active_thread;
  guard_execution = ex.guard_execution;
  guard_thread = ex.guard_thread;
  parent_guard_identifier = ex.parent_guard_identifier;
  reexecute_instruction = ex.reexecute_instruction;
  nondet_count = ex.nondet_count;
  dynamic_counter = ex.dynamic_counter;
  node_id = ex.node_id;

  CS_number = ex.CS_number;
  TS_number = ex.TS_number;
  return *this;
}

execution_statet::~execution_statet()
{

  delete target;

  // Free all name strings and suchlike we generated on this run
  // and no longer require
  // But, not if we're running with --schedule, as we'll need all
  // that information later.
  if (!is_schedule)
    string_container.restore_state_snapshot(str_state);
};


/*******************************************************************
   Function: execution_statet::get_active_state

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

goto_symex_statet &
execution_statet::get_active_state() {

  return threads_state.at(active_thread);
}

const goto_symex_statet &
execution_statet::get_active_state() const
{
  return threads_state.at(active_thread);
}

/*******************************************************************
   Function: execution_statet::all_threads_ended

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

bool
execution_statet::all_threads_ended()
{

  for (unsigned int i = 0; i < threads_state.size(); i++)
    if (!threads_state.at(i).thread_ended)
      return false;
  return true;
}

/*******************************************************************
   Function: execution_statet::get_active_atomic_number

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

unsigned int
execution_statet::get_active_atomic_number()
{

  return atomic_numbers.at(active_thread);
}

/*******************************************************************
   Function: execution_statet::increment_active_atomic_number

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::increment_active_atomic_number()
{

  atomic_numbers.at(active_thread)++;
}

/*******************************************************************
   Function: execution_statet::decrement_active_atomic_number

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::decrement_active_atomic_number()
{

  atomic_numbers.at(active_thread)--;
}

/*******************************************************************
   Function: execution_statet::get_guard_identifier

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

irep_idt
execution_statet::get_guard_identifier()
{

  return id2string(guard_execution) + '@' + i2string(CS_number) + '_' +
         i2string(last_active_thread) + '_' + i2string(node_id) + '&' +
         i2string(
           node_id) + "#1";
}

/*******************************************************************
   Function: execution_statet::get_guard_identifier_base

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

irep_idt
execution_statet::get_guard_identifier_base()
{

  return id2string(guard_execution) + '@' + i2string(CS_number) + '_' +
         i2string(last_active_thread) + '_' + i2string(node_id);
}


/*******************************************************************
   Function: execution_statet::set_parent_guard

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::set_parent_guard(const irep_idt & parent_guard)
{

  parent_guard_identifier = parent_guard;
}

/*******************************************************************
   Function: execution_statet::set_active_stat

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::set_active_state(unsigned int i)
{

  last_active_thread = active_thread;
  active_thread = i;
}

/*******************************************************************
   Function: execution_statet::decrement_trds_in_run

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::decrement_trds_in_run(void)
{

  typet int_t = int_type();
  exprt one_expr = gen_one(int_t);
  exprt lhs_expr = symbol_exprt("c::trds_in_run", int_t);
  exprt op1 = lhs_expr;
  exprt rhs_expr = gen_binary(exprt::minus, int_t, op1, one_expr);

  get_active_state().rename(rhs_expr, ns, node_id);
  base_type(rhs_expr, ns);
  simplify(rhs_expr);

  exprt new_lhs = lhs_expr;

  get_active_state().assignment(new_lhs, rhs_expr, ns, true, *this, node_id);

  target->assignment(
    get_active_state().guard,
    new_lhs, lhs_expr,
    rhs_expr,
    get_active_state().source,
    get_active_state().gen_stack_trace(),
    symex_targett::STATE);
}

/*******************************************************************
   Function: execution_statet::end_thread

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::end_thread(void)
{

  get_active_state().thread_ended = true;
  decrement_trds_in_run();
}

/*******************************************************************
   Function: execution_statet::increment_trds_in_run

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::increment_trds_in_run(void)
{

  static bool thrds_in_run_flag = 1;
  typet int_t = int_type();

  if (thrds_in_run_flag) {
    exprt lhs_expr = symbol_exprt("c::trds_in_run", int_t);
    constant_exprt rhs_expr(int_t);
    rhs_expr.set_value(integer2binary(1, config.ansi_c.int_width));

    get_active_state().assignment(lhs_expr, rhs_expr, ns, true, *this, node_id);

    thrds_in_run_flag = 0;
  }

  exprt one_expr = gen_one(int_t);
  exprt lhs_expr = symbol_exprt("c::trds_in_run", int_t);
  exprt op1 = lhs_expr;
  exprt rhs_expr = gen_binary(exprt::plus, int_t, op1, one_expr);

  get_active_state().rename(rhs_expr, ns, node_id);
  base_type(rhs_expr, ns);
  simplify(rhs_expr);

  exprt new_lhs = lhs_expr;

  get_active_state().assignment(new_lhs, rhs_expr, ns, true, *this, node_id);

  target->assignment(
    get_active_state().guard,
    new_lhs, lhs_expr,
    rhs_expr,
    get_active_state().source,
    get_active_state().gen_stack_trace(),
    symex_targett::STATE);
}

/*******************************************************************
   Function:  execution_statet::update_trds_count

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::update_trds_count(void)
{

  typet int_t = int_type();
  exprt lhs_expr = symbol_exprt("c::trds_count", int_t);
  exprt op1 = lhs_expr;

  constant_exprt rhs_expr = constant_exprt(int_t);
  rhs_expr.set_value(integer2binary(threads_state.size() - 1,
                                    config.ansi_c.int_width));
  get_active_state().rename(rhs_expr, ns, node_id);
  base_type(rhs_expr, ns);
  simplify(rhs_expr);

  exprt new_lhs = lhs_expr;

  get_active_state().assignment(new_lhs, rhs_expr, ns, true, *this, node_id);

  target->assignment(
    get_active_state().guard,
    new_lhs, lhs_expr,
    rhs_expr,
    get_active_state().source,
    get_active_state().gen_stack_trace(),
    symex_targett::STATE);
}

/*******************************************************************
   Function: execution_statet::execute_guard

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::execute_guard(const namespacet &ns)
{

  node_id = node_count++;
  exprt guard_expr = symbol_exprt(get_guard_identifier_base(), bool_typet());
  exprt parent_guard;
  exprt new_lhs = guard_expr;

  typet my_type = uint_type();
  exprt trd_expr = symbol_exprt(get_guard_identifier_base(), my_type);
  constant_exprt num_expr = constant_exprt(my_type);
  num_expr.set_value(integer2binary(active_thread, config.ansi_c.int_width));
  exprt cur_rhs = equality_exprt(trd_expr, num_expr);

  exprt new_rhs;
  parent_guard = true_exprt();
  new_rhs = parent_guard;

  if (!parent_guard_identifier.empty()) {
    parent_guard = symbol_exprt(parent_guard_identifier, bool_typet());
    new_rhs = cur_rhs;   //gen_and(parent_guard, cur_rhs);
  }

  get_active_state().assignment(new_lhs, new_rhs, ns, false, *this, node_id);

  assert(new_lhs.identifier() == get_guard_identifier());

  guardt old_guard;
  old_guard.add(parent_guard);
  exprt new_guard_expr = symbol_exprt(get_guard_identifier(), bool_typet());

  guardt guard;
  target->assignment(
    guard,
    new_lhs, guard_expr,
    new_rhs,
    get_active_state().source,
    get_active_state().gen_stack_trace(),
    symex_targett::HIDDEN);

  // copy the new guard exprt to every threads
  for (unsigned int i = 0; i < threads_state.size(); i++)
  {
    // remove the old guard first
    threads_state.at(i).guard -= old_guard;
    threads_state.at(i).guard.add(new_guard_expr);
  }
}

/*******************************************************************
   Function: execution_statet::add_thread

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::add_thread(goto_programt::const_targett thread_start,
  goto_programt::const_targett thread_end,
  const goto_programt *prog)
{

  goto_symex_statet state(state_level2);
  state.initialize(thread_start, thread_end, prog, threads_state.size());

  threads_state.push_back(state);
  atomic_numbers.push_back(0);

  if (DFS_traversed.size() <= state.source.thread_nr) {
    DFS_traversed.push_back(false);
  } else {
    DFS_traversed[state.source.thread_nr] = false;
  }

  exprs_read_write.push_back(read_write_set());
}

/*******************************************************************
   Function: execution_statet::add_thread

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

void
execution_statet::add_thread(goto_symex_statet & state)
{

  goto_symex_statet new_state(state);

  new_state.source.thread_nr = threads_state.size();
  threads_state.push_back(new_state);
  atomic_numbers.push_back(0);

  if (DFS_traversed.size() <= new_state.source.thread_nr) {
    DFS_traversed.push_back(false);
  } else {
    DFS_traversed[new_state.source.thread_nr] = false;
  }
  exprs_read_write.push_back(read_write_set());
}

/*******************************************************************
   Function: execution_statet::get_expr_write_globals

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

unsigned int
execution_statet::get_expr_write_globals(const namespacet &ns,
  const exprt & expr)
{

  std::string identifier = expr.identifier().as_string();

  if (expr.id() == exprt::addrof ||
      expr.id() == "valid_object" ||
      expr.id() == "dynamic_size" ||
      expr.id() == "dynamic_type" ||
      expr.id() == "is_zero_string" ||
      expr.id() == "zero_string" ||
      expr.id() == "zero_string_length")
    return 0;
  else if (expr.id() == exprt::symbol) {
    const irep_idt &id = expr.identifier();
    const irep_idt &identifier = get_active_state().get_original_name(id);
    const symbolt &symbol = ns.lookup(identifier);
    if (identifier == "c::__ESBMC_alloc"
        || identifier == "c::__ESBMC_alloc_size")
      return 0;
    else if ((symbol.static_lifetime || symbol.type.is_dynamic_set())) {
      exprs_read_write.at(active_thread).write_set.insert(identifier);
      return 1;
    } else
      return 0;
  }

  unsigned int globals = 0;

  forall_operands(it, expr) {
    globals += get_expr_write_globals(ns, *it);
  }

  return globals;
}

/*******************************************************************
   Function: execution_statet::get_expr_read_globals

   Inputs:

   Outputs:

   Purpose:

 \*******************************************************************/

unsigned int
execution_statet::get_expr_read_globals(const namespacet &ns,
  const exprt & expr)
{

  std::string identifier = expr.identifier().as_string();

  if (expr.id() == exprt::addrof ||
      expr.type().id() == typet::t_pointer ||
      expr.id() == "valid_object" ||
      expr.id() == "dynamic_size" ||
      expr.id() == "dynamic_type" ||
      expr.id() == "is_zero_string" ||
      expr.id() == "zero_string" ||
      expr.id() == "zero_string_length")
    return 0;
  else if (expr.id() == exprt::symbol) {
    const irep_idt &id = expr.identifier();
    const irep_idt &identifier = get_active_state().get_original_name(id);

    if (identifier == "goto_symex::\\guard!" +
        i2string(get_active_state().top().level1._thread_id))
      return 0;

    const symbolt *symbol;
    if (ns.lookup(identifier, symbol))
      return 0;

    if (identifier == "c::__ESBMC_alloc" || identifier ==
        "c::__ESBMC_alloc_size")
      return 0;
    else if ((symbol->static_lifetime || symbol->type.is_dynamic_set())) {
      exprs_read_write.at(active_thread).read_set.insert(identifier);
      return 1;
    } else
      return 0;
  }
  unsigned int globals = 0;

  forall_operands(it, expr) {
    globals += get_expr_read_globals(ns, *it);
  }

  return globals;
}

crypto_hash
execution_statet::generate_hash(void) const
{

  crypto_hash state = state_level2.generate_l2_state_hash();
  std::string str = state.to_string();

  for (std::vector<goto_symex_statet>::const_iterator it = threads_state.begin();
       it != threads_state.end(); it++) {
    goto_programt::const_targett pc = it->source.pc;
    int id = pc->location_number;
    std::stringstream s;
    s << id;
    str += "!" + s.str();
  }

  crypto_hash h = crypto_hash(str);

  return h;
}

std::string
unmunge_SSA_name(std::string str)
{
  size_t and_pos, hash_pos;
  std::string result;

  /* All SSA assignment names are of the form symname&x_x_x#n, where n is the
     assignment count for that symbol, and x are a variety of uninteresting but
     interleaving-specific numbers. So, we want to discard them. */
  and_pos = str.find("&");
  hash_pos = str.rfind("#");
  result = str.substr(0, and_pos);
  result = result + str[hash_pos + 1];
  return result;
}

static std::string state_to_ignore[8] =
{
  "\\guard", "trds_count", "trds_in_run", "deadlock_wait", "deadlock_mutex",
  "count_lock", "count_wait", "unlocked"
};

std::string
execution_statet::serialise_expr(const exprt &rhs)
{
  std::string str;
  uint64_t val;
  int i;

  // FIXME: some way to disambiguate what's part of a hash / const /whatever,
  // and what's part of an operator

  // The plan: serialise this expression into the identifiers of its operations,
  // replacing symbol names with the hash of their value.
  if (rhs.id() == exprt::symbol) {

    str = rhs.identifier().as_string();
    for (i = 0; i < 8; i++)
      if (str.find(state_to_ignore[i]) != std::string::npos)
	return "(ignore)";

    // If this is something we've already encountered, use the hash of its
    // value.
    exprt tmp = rhs;
    get_active_state().get_original_name(tmp);
    if (state_level2.current_hashes.find(tmp.identifier().as_string()) !=
        state_level2.current_hashes.end()) {
      crypto_hash h = state_level2.current_hashes.find(
        tmp.identifier().as_string())->second;
      return "hash(" + h.to_string() + ")";
    }

    /* Otherwise, it's something that's been assumed, or some form of
       nondeterminism. Just return its name. */
    return rhs.identifier().as_string();
  } else if (rhs.id() == exprt::arrayof) {
    /* An array of the same set of values: generate all of them. */
    str = "array(";
    irept array = rhs.type();
    exprt size = (exprt &)array.size_irep();
    str += "sz(" + serialise_expr(size) + "),";
    str += "elem(" + serialise_expr(rhs.op0()) + "))";
  } else if (rhs.id() == exprt::with) {
    exprt rec = rhs;

    if (rec.type().id() == typet::t_array) {
      str = "array(";
      str += "prev(" + serialise_expr(rec.op0()) + "),";
      str += "idx(" + serialise_expr(rec.op1()) + "),";
      str += "val(" + serialise_expr(rec.op2()) + "))";
    } else if (rec.type().id() == typet::t_struct) {
      str = "struct(";
      str += "prev(" + serialise_expr(rec.op0()) + "),";
      str += "member(" + serialise_expr(rec.op1()) + "),";
      str += "val(" + serialise_expr(rec.op2()) + "),";
    } else if (rec.type().id() ==  typet::t_union) {
      /* We don't care about previous assignments to this union, because they're
         overwritten by this one, and leads to undefined side effects anyway.
         So, just serialise the identifier, the member assigned to, and the
         value assigned */
      str = "union_set(";
      str += "union_sym(" + rec.op0().identifier().as_string() + "),";
      str += "field(" + serialise_expr(rec.op1()) + "),";
      str += "val(" + serialise_expr(rec.op2()) + "))";
    } else {
      throw "Unrecognised type of with expression: " +
            rec.op0().type().id().as_string();
    }
  } else if (rhs.id() == exprt::index) {
    str = "index(";
    str += serialise_expr(rhs.op0());
    str += ",idx(" + serialise_expr(rhs.op1()) + ")";
  } else if (rhs.id() == "member_name") {
    str = "component(" + rhs.component_name().as_string() + ")";
  } else if (rhs.id() == exprt::member) {
    str = "member(entity(" + serialise_expr(rhs.op0()) + "),";
    str += "member_name(" + rhs.component_name().as_string() + "))";
  } else if (rhs.id() == "nondet_symbol") {
    /* Just return the identifier: it'll be unique to this particular piece of
       entropy */
    exprt tmp = rhs;
    get_active_state().get_original_name(tmp);
    str = "nondet_symbol(" + tmp.identifier().as_string() + ")";
  } else if (rhs.id() == exprt::i_if) {
    str = "cond(if(" + serialise_expr(rhs.op0()) + "),";
    str += "then(" + serialise_expr(rhs.op1()) + "),";
    str += "else(" + serialise_expr(rhs.op2()) + "))";
  } else if (rhs.id() == "struct") {
    str = rhs.type().tag().as_string();
    str = "struct(tag(" + str + "),";
    forall_operands(it, rhs) {
      str = str + "(" + serialise_expr(*it) + "),";
    }
    str += ")";
  } else if (rhs.id() == "union") {
    str = rhs.type().tag().as_string();
    str = "union(tag(" + str + "),";
    forall_operands(it, rhs) {
      str = str + "(" + serialise_expr(*it) + "),";
    }
  } else if (rhs.id() == exprt::constant) {
    // It appears constants can be "true", "false", or a bit vector. Parse that,
    // and then print the value as a base 10 integer.

    irep_idt idt_val = rhs.value();
    if (idt_val == exprt::i_true) {
      val = 1;
    } else if (idt_val == exprt::i_false) {
      val = 0;
    } else {
      val = strtol(idt_val.c_str(), NULL, 2);
    }

    std::stringstream tmp;
    tmp << val;
    str = "const(" + tmp.str() + ")";
  } else if (rhs.id() == "pointer_offset") {
    str = "pointer_offset(" + serialise_expr(rhs.op0()) + ")";
  } else if (rhs.id() == "string-constant") {
    exprt tmp;
    string2array(rhs, tmp);
    return serialise_expr(tmp);
  } else if (rhs.id() == "same-object") {
  } else if (rhs.id() == "byte_update_little_endian") {
  } else if (rhs.id() == "byte_update_big_endian") {
  } else if (rhs.id() == "byte_extract_little_endian") {
  } else if (rhs.id() == "byte_extract_big_endian") {
  } else if (rhs.id() == "infinity") {
    return "inf";
  } else {
    execution_statet::expr_id_map_t::const_iterator it;
    it = expr_id_map.find(rhs.id());
    if (it != expr_id_map.end())
      return it->second(*this, rhs);

    std::cout << "Unrecognized expression when generating state hash:\n";
    std::cout << rhs.pretty(0) << std::endl;
    abort();
  }

  return str;
}

// If we have a normal expression, either arithmatic, binary, comparision,
// or whatever, just take the operator and append its operands.
std::string
serialise_normal_operation(execution_statet &ex_state, const exprt &rhs)
{
  std::string str;

  str = rhs.id().as_string();
  forall_operands(it, rhs) {
    str = str + "(" + ex_state.serialise_expr(*it) + ")";
  }

  return str;
}


crypto_hash
execution_statet::update_hash_for_assignment(const exprt &rhs)
{

  return crypto_hash(serialise_expr(rhs));
}

execution_statet::expr_id_map_t execution_statet::expr_id_map;

execution_statet::expr_id_map_t
execution_statet::init_expr_id_map()
{
  execution_statet::expr_id_map_t m;
  m[exprt::plus] = serialise_normal_operation;
  m[exprt::minus] = serialise_normal_operation;
  m[exprt::mult] = serialise_normal_operation;
  m[exprt::div] = serialise_normal_operation;
  m[exprt::mod] = serialise_normal_operation;
  m[exprt::equality] = serialise_normal_operation;
  m[exprt::implies] = serialise_normal_operation;
  m[exprt::i_and] = serialise_normal_operation;
  m[exprt::i_xor] = serialise_normal_operation;
  m[exprt::i_or] = serialise_normal_operation;
  m[exprt::i_not] = serialise_normal_operation;
  m[exprt::notequal] = serialise_normal_operation;
  m["unary-"] = serialise_normal_operation;
  m["unary+"] = serialise_normal_operation;
  m[exprt::abs] = serialise_normal_operation;
  m[exprt::isnan] = serialise_normal_operation;
  m[exprt::i_ge] = serialise_normal_operation;
  m[exprt::i_gt] = serialise_normal_operation;
  m[exprt::i_le] = serialise_normal_operation;
  m[exprt::i_lt] = serialise_normal_operation;
  m[exprt::i_bitand] = serialise_normal_operation;
  m[exprt::i_bitor] = serialise_normal_operation;
  m[exprt::i_bitxor] = serialise_normal_operation;
  m[exprt::i_bitnand] = serialise_normal_operation;
  m[exprt::i_bitnor] = serialise_normal_operation;
  m[exprt::i_bitnxor] = serialise_normal_operation;
  m[exprt::i_bitnot] = serialise_normal_operation;
  m[exprt::i_shl] = serialise_normal_operation;
  m[exprt::i_lshr] = serialise_normal_operation;
  m[exprt::i_ashr] = serialise_normal_operation;
  m[exprt::typecast] = serialise_normal_operation;
  m[exprt::addrof] = serialise_normal_operation;
  m["pointer_obj"] = serialise_normal_operation;
  m["pointer_object"] = serialise_normal_operation;

  return m;
}

void
execution_statet::print_stack_traces(const namespacet &ns,
  unsigned int indent) const
{
  std::vector<goto_symex_statet>::const_iterator it;
  std::string spaces = std::string("");
  int i;

  for (i = 0; i < indent; i++)
    spaces += " ";

  i = 0;
  for (it = threads_state.begin(); it != threads_state.end(); it++) {
    std::cout << spaces << "Thread " << i++ << ":" << std::endl;
    it->print_stack_trace(ns, indent + 2);
    std::cout << std::endl;
  }

  return;
}

bool execution_statet::expr_id_map_initialized = false;
