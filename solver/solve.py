

import json
import sys
import re
from ortools.linear_solver import pywraplp


def parse_expr(expr_str, var_objs):

    expr_str = expr_str.strip()
    if not expr_str:
        return 0


    term_regex = r'([+-]?\d*\.?\d*)\s*\*?\s*([a-zA-Z_]\w*)|([+-]?\d+\.?\d*)'
    
    result = None
    current_sign = 1.0
    
    for match in re.finditer(term_regex, expr_str):
        coeff_str, var_name, number = match.groups()
        
        if number is not None and var_name is None:

            return float(number)
        
        if coeff_str == '':
            coeff = current_sign
        elif coeff_str == '+':
            coeff = current_sign
        elif coeff_str == '-':
            coeff = -current_sign
        else:
            coeff = float(coeff_str) * current_sign
        
        current_sign = 1.0
        
        if var_name is not None:
            if var_name not in var_objs:
                raise ValueError(f"Unknown variable: {var_name}")
            var_term = coeff * var_objs[var_name]
            if result is None:
                result = var_term
            else:
                result = result + var_term
        

        if expr_str[match.end():match.end()+1] == '-':
            current_sign = -1.0
        elif expr_str[match.end():match.end()+1] == '+':
            current_sign = 1.0

    return result if result is not None else 0


def parse_constraint(constraint_str, var_objs):

    constraint_str = constraint_str.strip()
    
    if ">=" in constraint_str:
        parts = constraint_str.split(">=", 1)
        op = ">="
    elif "<=" in constraint_str:
        parts = constraint_str.split("<=", 1)
        op = "<="
    elif "=" in constraint_str:
        parts = constraint_str.split("=", 1)
        op = "="
    else:
        raise ValueError(f"Invalid constraint: {constraint_str}")

    lhs_str = parts[0].strip()
    rhs_str = parts[1].strip()


    try:
        rhs_val = float(eval(rhs_str, {"__builtins__": {}}))
        rhs_expr = None
    except (NameError, SyntaxError):
        rhs_expr = parse_expr(rhs_str, var_objs)
        rhs_val = 0.0

    lhs_expr = parse_expr(lhs_str, var_objs)
    return lhs_expr, op, rhs_val, rhs_expr


def solve_ip(variables, constraints, objective, obj_type, integer_vars):

    solver = pywraplp.Solver.CreateSolver("SCIP")


    var_objs = {}
    for v in variables:
        is_int = v in integer_vars
        if is_int:
            var_objs[v] = solver.IntVar(0, solver.infinity(), v)
        else:
            var_objs[v] = solver.NumVar(0, solver.infinity(), v)


    for constraint in constraints:
        lhs, op, rhs_val, rhs_expr = parse_constraint(constraint, var_objs)
        if rhs_expr is None:
            if op == "<=":
                solver.Add(lhs <= rhs_val)
            elif op == ">=":
                solver.Add(lhs >= rhs_val)
            elif op == "=":
                solver.Add(lhs == rhs_val)
        else:
            diff = lhs - rhs_expr
            if op == "<=":
                solver.Add(diff <= rhs_val)
            elif op == ">=":
                solver.Add(diff >= rhs_val)
            elif op == "=":
                solver.Add(diff == rhs_val)


    obj_expr = parse_expr(objective, var_objs)
    if obj_type in ("max", "maximize"):
        solver.Maximize(obj_expr)
    else:
        solver.Minimize(obj_expr)


    status = solver.Solve()


    if status == pywraplp.Solver.OPTIMAL:
        solution = {v: var_objs[v].solution_value() for v in variables}
        return {
            "status": "OPTIMAL",
            "solution": solution,
            "objective_value": solver.Objective().Value()
        }
    elif status == pywraplp.Solver.FEASIBLE:
        solution = {v: var_objs[v].solution_value() for v in variables}
        return {
            "status": "FEASIBLE",
            "solution": solution,
            "objective_value": solver.Objective().Value()
        }
    elif status == pywraplp.Solver.INFEASIBLE:
        return {"status": "INFEASIBLE", "solution": {}, "objective_value": 0}
    elif status == pywraplp.Solver.UNBOUNDED:
        return {"status": "UNBOUNDED", "solution": {}, "objective_value": 0}
    else:
        return {"status": "UNKNOWN", "solution": {}, "objective_value": 0}


def main():
    try:
        data = json.loads(sys.stdin.read())
    except:
        print(json.dumps({"status": "ERROR", "message": "Invalid JSON input"}))
        return

    variables = data.get("variables", [])
    constraints = data.get("constraints", [])
    objective = data.get("objective", "0")
    obj_type = data.get("type", "maximize")
    integer_vars = data.get("integer_vars", variables)

    result = solve_ip(variables, constraints, objective, obj_type, integer_vars)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
