VarRecord *IRO_GetTemp(IROExpr *expr)
{
    expr->temp = create_temp_object(expr->linear->rtype);
    return fn_0044ba70(expr->temp, 1, 1);
}
