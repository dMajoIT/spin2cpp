//
// Bytecode (nucode) compiler for spin2cpp
//
// Copyright 2021 Total Spectrum Software Inc.
// see the file COPYING for conditions of redistribution
//
#include "outnu.h"
#include "becommon.h"
#include <stdlib.h>

#define NumRetLongs(F) FuncLongResults(F->overalltype)
#define NumArgLongs(F) FuncLongParams(F->overalltype)
#define NumLocalLongs(F) ((F)->numlocals)

static void NuCompileStatement(NuIrList *irl, AST *ast); // forward declaration
static void NuCompileStmtlist(NuIrList *irl, AST *ast); // forward declaration
static int NuCompileExpression(NuIrList *irl, AST *ast); // returns number of items left on stack
static int NuCompileExprList(NuIrList *irl, AST *ast); // returns number of items left on stack
static NuIrOpcode NuCompileLhsAddress(NuIrList *irl, AST *lhs); // returns op used for loading
static void NuCompileMul(NuIrList *irl, AST *lhs, AST *rhs, int gethi);

typedef struct NuLabelList {
    struct NuLabelList *next;
    NuIrLabel *label;
} NuLabelList;

static NuLabelList quitstack;
static NuIrLabel *nextlabel, *quitlabel;

static void NuPushQuitNext(NuIrLabel *q, NuIrLabel *n) {
    NuLabelList *qholder = (NuLabelList *)malloc(sizeof(NuLabelList));
    NuLabelList *nholder = (NuLabelList *)malloc(sizeof(NuLabelList));
    qholder->label = quitlabel;
    nholder->label = nextlabel;
    qholder->next = nholder;
    nholder->next = quitstack.next;
    quitstack.next = qholder;

    quitlabel = q;
    nextlabel = n;
}
static void NuPopQuitNext() {
    NuLabelList *ql, *nl;
    ql = quitstack.next;
    if (!ql || !ql->next) {
        ERROR(NULL, "Internal error: empty loop stack");
        return;
    }
    nl = ql->next;
    quitstack.next = nl->next;
    quitlabel = ql->label;
    nextlabel = nl->label;
    free(nl);
    free(ql);    
}

static NuIrLabel *
NuIrOffsetLabel(NuIrLabel *base, int offset) {
    NuIrLabel *r = malloc(sizeof(r));
    *r = *base;
    r->offset = offset;
    return r;
}

static void
NuPrepareModuleBedata(Module *P) {
    // Init bedata
    if (P->bedata) return;

    DEBUG(NULL,"Preparing object %s",P->fullname);

    P->bedata = calloc(sizeof(NuModData), 1);
    ModData(P)->datLabel = NuCreateLabel();
}

static void
NuPrepareFunctionBedata(Function *F) {
    if (F->bedata) {
        return;
    }
    F->bedata = calloc(sizeof(NuFunData),1);
    
    FunData(F)->entryLabel = NuCreateLabel();
    //FunData(F)->exitLabel = NuCreateLabel();    
}

// drop n items from the stack
static void NuCompileDrop(NuIrList *irl, int n) {
    while (n > 1) {
        NuEmitOp(irl, NU_OP_DROP2);
        n -= 2;
    }
    if (n) {
        NuEmitOp(irl, NU_OP_DROP);
    }
}

static int NuCompileFunCall(NuIrList *irl, AST *node) {
    int pushed;
    Function *func = NULL;
    AST *functype = NULL;
    AST *objref = NULL;
    Symbol *sym;
    AST *args = node->right;
    
    pushed = NuCompileExprList(irl, args);
    sym = FindFuncSymbol(node, &objref, 1);
    if (sym && sym->kind == SYM_FUNCTION) {
        func = (Function *)sym->val;
        pushed = func->numresults;
        if (!func->body) {
            if (pushed) {
                ERROR(node, "empty function with results expected...");
            }
            return 0;
        }
        if (func->body->kind == AST_BYTECODE) {
            NuEmitNamedOpcode(irl, func->body->d.string);
        } else {
            // output a CALL
            NuPrepareFunctionBedata(func);
            if (func->is_static || func->module == current) {
                // plain CALL is OK
                NuEmitAddress(irl, FunData(func)->entryLabel);
                NuEmitOp(irl, NU_OP_CALL);
            } else if (func->module == systemModule) {
                // system modules don't actually have non-static functions, we're just
                WARNING(node, "non-static system module function called");
                // plain CALL is OK
                NuEmitAddress(irl, FunData(func)->entryLabel);
                NuEmitOp(irl, NU_OP_CALL);
            } else {
                ERROR(node, "Unable to compile method calls");
            }
        }
    } else {
        functype = ExprType(node->left);
        (void)functype;
        ERROR(node, "Unable to compile indirect function calls");
        return 0;
    }
    return pushed;
}

// find the load opcode corresponding to a store opcode
static NuIrOpcode NuLoadOpFor(NuIrOpcode stOp) {
    switch (stOp) {
    case NU_OP_STB: return NU_OP_LDB;
    case NU_OP_STW: return NU_OP_LDW;
    case NU_OP_STL: return NU_OP_LDL;
    case NU_OP_STD: return NU_OP_LDD;
    case NU_OP_STREG: return NU_OP_LDREG;
    default:
        ERROR(NULL, "bad kind of store op");
        return NU_OP_ILLEGAL;
    }
}

// find how to load a particular type, or return NU_OP_ILLEGAL if we don't know how
static NuIrOpcode LoadStoreOp(AST *typ, int isLoad)
{
    int siz;
    NuIrOpcode op = NU_OP_ILLEGAL;

    if (IsArrayType(typ)) {
        typ = BaseType(typ);
    }
    siz = TypeSize(typ);
    switch (siz) {
    case 0:
        ERROR(NULL, "Attempt to use void value");
        return op;
    case 1:
        op = isLoad ? NU_OP_LDB : NU_OP_STB;
        break;
    case 2:
        op = isLoad ? NU_OP_LDW : NU_OP_STW;
        break;
    case 4:
        op = isLoad ? NU_OP_LDL : NU_OP_STL;
        break;
    case 8:
        op = isLoad ? NU_OP_LDD : NU_OP_STD;
        break;
    default:
        break;
    }
    return op;
}

/* push effective address of an identifier onto the stack */
/* returns an opcode for how to load this identifier, if we know */
static NuIrOpcode
NuCompileIdentifierAddress(NuIrList *irl, AST *node, int isLoad)
{
    Symbol *sym = LookupAstSymbol(node,NULL);
    NuIrOpcode offsetOp = NU_OP_ADD_DBASE;
    NuIrOpcode loadOp = NU_OP_ILLEGAL;
    int offset;
    
    if (!sym) {
        ERROR(node, "identifier %s not found", GetUserIdentifierName(node));
        return loadOp;
    }
    switch (sym->kind) {
    case SYM_LOCALVAR:
    case SYM_TEMPVAR:
    case SYM_PARAMETER:
    case SYM_RESULT:
        offset = sym->offset;
        loadOp = LoadStoreOp(sym->val, isLoad);
        break;
    case SYM_VARIABLE:
        if (sym->flags & SYMF_GLOBAL) {
            offset = sym->offset;
            if (offset < 0) {
                ERROR(node, "unhandled global %s", sym->user_name);
            }
            offsetOp = NU_OP_ILLEGAL;
        } else {
            offset = sym->offset;
            offsetOp = NU_OP_ADD_VBASE;
        }
        loadOp = LoadStoreOp(sym->val, isLoad);
        break;
    case SYM_LABEL:
    {
        Label *lab = sym->val;
        NuIrLabel *nulabel = NULL;
        uint32_t labelval = lab->hubval;
        Module *Q = sym->module;
        offset = labelval;
        loadOp = LoadStoreOp(lab->type, isLoad);
        if (!ModData(Q)) {
            NuPrepareModuleBedata(Q);
        }
        nulabel = ModData(Q)->datLabel;
        nulabel = NuIrOffsetLabel(nulabel, offset);
        NuEmitAddress(irl, nulabel);
        return loadOp;
    }
    default:
        ERROR(node, "Unhandled symbol type for %s", GetUserIdentifierName(node));
        return loadOp;
    }
    NuEmitConst(irl, offset);
    if (offsetOp != NU_OP_ILLEGAL) {
        NuEmitOp(irl, offsetOp);
    }
    return loadOp;
}

// figure out address of an array
static NuIrOpcode
NuCompileArrayAddress(NuIrList *irl, AST *node, int isLoad)
{
    NuIrOpcode op = NU_OP_ILLEGAL;
    AST *base = node->left;
    AST *index = node->right;
    AST *typ = ExprType(node);
    (void)NuCompileLhsAddress(irl, base); // op doesn't matter yet
    if (!IsConstZero(index)) {
        int siz = TypeSize(typ);
        if (siz > 1) {
            NuCompileMul(irl, index, AstInteger(siz), 0);
        } else {
            int n = NuCompileExpression(irl, index);
            if (n != 1) {
                ERROR(node, "Bad array indexing");
            }
        }
        NuEmitOp(irl, NU_OP_ADD);
    }
    op = LoadStoreOp(typ, isLoad);
    return op;
}

/* find opposite comparison for branch */
static NuIrOpcode OppositeCond(NuIrOpcode op) {
    switch (op) {
    case NU_OP_BRA:   return NU_OP_DROP2;
    case NU_OP_DROP2: return NU_OP_BRA;
    case NU_OP_CBEQ:  return NU_OP_CBNE;
    case NU_OP_CBNE:  return NU_OP_CBEQ;
    case NU_OP_CBLTS: return NU_OP_CBGES;
    case NU_OP_CBGES: return NU_OP_CBLTS;
    case NU_OP_CBLES: return NU_OP_CBGTS;
    case NU_OP_CBGTS: return NU_OP_CBLES;
    case NU_OP_CBLTU: return NU_OP_CBGEU;
    case NU_OP_CBGEU: return NU_OP_CBLTU;
    case NU_OP_CBLEU: return NU_OP_CBGTU;
    case NU_OP_CBGTU: return NU_OP_CBLEU;
    default: ERROR(NULL, "Bad opcode to OppositeCond");
    }
    return NU_OP_ILLEGAL;
}

/* compile a boolean expression */
/* returns the opcode to use for the test */
static NuIrOpcode
NuCompileBasicBoolExpression(NuIrList *irl, AST *expr)
{
    NuIrOpcode opc = NU_OP_ILLEGAL;
    AST *left, *right;
    int n;
    
    int opkind = (expr->kind == AST_OPERATOR) ? expr->d.ival : -1;
    left = expr->left;
    right = expr->right;
    switch (opkind) {
    default:
        opc = NU_OP_CBNE; left = expr; right = AstInteger(0);
        break;
    case K_EQ: opc = NU_OP_CBEQ; break;
    case K_NE: opc = NU_OP_CBNE; break;
    case '<':  opc = NU_OP_CBLTS; break;
    case K_LE: opc = NU_OP_CBLES; break;
    case K_LTU: opc = NU_OP_CBLTU; break;
    case K_LEU: opc = NU_OP_CBLEU; break;
    case '>':   opc = NU_OP_CBGTS; break;
    case K_GE:  opc = NU_OP_CBGES; break;
    case K_GTU: opc = NU_OP_CBGTU; break;
    case K_GEU: opc = NU_OP_CBGEU; break;
    }
    n = NuCompileExpression(irl, left);
    if (n != 1) { ERROR(left, "Expected single value in boolean expression"); }
    n = NuCompileExpression(irl, right);
    if (n != 1) { ERROR(left, "Expected single value in boolean expression"); }
    return opc;
}

/* compile a boolean branch */
static void
NuCompileBoolBranches(NuIrList *irl, AST *expr, NuIrLabel *truedest, NuIrLabel *falsedest)
{
    NuIrLabel *dummylabel = NULL;
    int opkind;
    NuIrOpcode opc;
    
    if (IsConstExpr(expr)) {
        int x = EvalConstExpr(expr);
        if (x && truedest) NuEmitBranch(irl, NU_OP_BRA, truedest);
        if (!x && falsedest) NuEmitBranch(irl, NU_OP_BRA, falsedest);
    }
    if (expr->kind == AST_ISBETWEEN) {
        ERROR(expr, "Cannot handle ISBETWEEN yet");
        return;
    }
    if (expr->kind == AST_OPERATOR) {
        opkind = expr->d.ival;
    } else {
        opkind = -1;
    }
    switch (opkind) {
    case K_BOOL_NOT:
        NuCompileBoolBranches(irl, expr->right, falsedest, truedest);
        break;
    case K_BOOL_AND:
        if (!falsedest) {
            falsedest = dummylabel = NuCreateLabel();
        }
        NuCompileBoolBranches(irl, expr->left, NULL, falsedest);
        NuCompileBoolBranches(irl, expr->right, truedest, falsedest);
        if (dummylabel) {
            NuEmitLabel(irl, dummylabel);
        }
        break;
    case K_BOOL_OR:
        if (!truedest) {
            truedest = dummylabel = NuCreateLabel();
        }
        NuCompileBoolBranches(irl, expr->left, truedest, NULL);
        NuCompileBoolBranches(irl, expr->right, truedest, falsedest);
        if (dummylabel) {
            NuEmitLabel(irl, dummylabel);
        }
        break;
    default:
        opc = NuCompileBasicBoolExpression(irl, expr);
        if (!truedest) {
            opc = OppositeCond(opc);
            truedest = falsedest;
            falsedest = NULL;
        }
        NuEmitBranch(irl, opc, truedest);
        if (falsedest && opc != NU_OP_BRA) {
            NuEmitBranch(irl, NU_OP_BRA, falsedest);
        }
        if (dummylabel) {
            NuEmitLabel(irl, dummylabel);
        }
        break;
    }       
}
/* compile address for lhs of assignment, return store op (or NU_OP_ILLEGAL if fail) */
static NuIrOpcode NuCompileLhsAddress(NuIrList *irl, AST *lhs)
{
    NuIrOpcode op = NU_OP_ILLEGAL;
    switch (lhs->kind) {
    case AST_IDENTIFIER:
    case AST_LOCAL_IDENTIFIER:
        op = NuCompileIdentifierAddress(irl, lhs, 0);
        break;
    case AST_ARRAYREF:
        op = NuCompileArrayAddress(irl, lhs, 0);
        break;
    case AST_MEMREF: {
        int n = NuCompileExpression(irl, lhs->right);
        AST *typ = lhs->left;
        op = LoadStoreOp(typ, 0);
        if (n != 1) {
            ERROR(lhs, "too many values pushed on stack");
        }
    } break;
    default:
        ERROR(lhs, "Address type not handled yet");
        break;
    }
    return op;
}

/* compile assignment */
/* returns number of longs left on stack */
static int NuCompileAssign(NuIrList *irl, AST *ast, int inExpression)
{
    AST *lhs = ast->left;
    AST *rhs = ast->right;
    int n;
    NuIrOpcode op;

    if (ast->d.ival != K_ASSIGN) {
        ERROR(ast, "Cannot handle complex assignment");
    }
    n = NuCompileExpression(irl, rhs);
    if (n == 0) {
        ERROR(rhs, "Assignment from void value");
    } else if (n != 1) {
        ERROR(rhs, "multiple assignment not handled yet");
    }
    if (inExpression) {
        NuEmitOp(irl, NU_OP_DUP); // leave last value on stack
    }
    op = NuCompileLhsAddress(irl, lhs);
    if (op == NU_OP_ILLEGAL) {
        ERROR(lhs, "Do not know how to store into this yet");
    } else {
        NuEmitOp(irl, op);
    }
    return inExpression;
}

static void
NuCompileMul(NuIrList *irl, AST *lhs, AST *rhs, int gethi)
{
    int isUnsigned = (gethi & 2);
    int needHi = (gethi & 1);
    AST *tmp;

    // optimize some constant multiplies
    // make sure constant is on the right
    if (IsConstExpr(lhs)) {
        if (IsConstExpr(rhs)) {
            NuEmitConst(irl, EvalConstExpr(lhs) * EvalConstExpr(rhs));
            return;
        }
        tmp = lhs; lhs = rhs; rhs = tmp;
    }
    NuCompileExpression(irl, lhs);
    if (IsConstExpr(rhs)) {
        // do some simple optimizations
        int r = EvalConstExpr(rhs);
        switch (r) {
        case 2:
            NuEmitOp(irl, NU_OP_DUP);
            NuEmitOp(irl, NU_OP_ADD);
            return;
        case 3:
            NuEmitOp(irl, NU_OP_DUP);
            NuEmitOp(irl, NU_OP_DUP);
            NuEmitOp(irl, NU_OP_ADD);
            NuEmitOp(irl, NU_OP_ADD);
            return;
        case 4:
            NuEmitConst(irl, 2);
            NuEmitOp(irl, NU_OP_SHL);
            return;
        case 8:
            NuEmitConst(irl, 3);
            NuEmitOp(irl, NU_OP_SHL);
            return;
        case 16:
            NuEmitConst(irl, 4);
            NuEmitOp(irl, NU_OP_SHL);
            return;
        default:
            break;
        }
    }
    NuCompileExpression(irl, rhs);
    if (isUnsigned) {
        NuEmitOp(irl, NU_OP_MULU);
    } else {
        NuEmitOp(irl, NU_OP_MULS);
    }
    // lo, hi are placed on stack
    if (needHi) {
        NuEmitOp(irl, NU_OP_SWAP);
    }
    NuEmitOp(irl, NU_OP_DROP);
}

static void
NuCompileDiv(NuIrList *irl, AST *expr, int gethi)
{
    AST *lhs = expr->left;
    AST *rhs = expr->right;
    int isUnsigned = (gethi & 2);
    int needHi = (gethi & 1);

    // FIXME: should optimize here for shifts and such
    NuCompileExpression(irl, lhs);
    NuCompileExpression(irl, rhs);
    if (isUnsigned) {
        NuEmitOp(irl, NU_OP_DIVU);
    } else {
        NuEmitOp(irl, NU_OP_DIVS);
    }
    // lo, hi are placed on stack
    if (needHi) {
        NuEmitOp(irl, NU_OP_SWAP);
    }
    NuEmitOp(irl, NU_OP_DROP);
}

/* returns number of longs pushed on stack */
static int
NuCompileOperator(NuIrList *irl, AST *node) {
    AST *lhs, *rhs;
    int optoken;
    int pushed = 0;
    
    lhs = node->left;
    rhs = node->right;
    optoken = node->d.ival;

    if (optoken == K_INCREMENT || optoken == K_DECREMENT) {
        // handle some special cases here
        NuIrOpcode math = (optoken == K_INCREMENT) ? NU_OP_ADD : NU_OP_SUB;
        NuIrOpcode ldOp, stOp;
        pushed = 1;
        if (lhs) {
            stOp = NuCompileLhsAddress(irl, lhs);
            if (stOp == NU_OP_ILLEGAL) return pushed;
            ldOp = NuLoadOpFor(stOp);
            NuEmitOp(irl, NU_OP_DUP); // duplicate address : stack has A A
            NuEmitOp(irl, ldOp);      // load: stack has A origV
            NuEmitOp(irl, NU_OP_SWAP); // stack has origV A
            NuEmitOp(irl, NU_OP_OVER); // stack has origV A origV
            NuEmitConst(irl, 1);
            NuEmitOp(irl, math);      // stack has origV A newV
            NuEmitOp(irl, NU_OP_SWAP); // stack has origV newV A
            NuEmitOp(irl, stOp);      // stack has origV
        } else {
            // inc/dec first
            stOp = NuCompileLhsAddress(irl, rhs);
            if (stOp == NU_OP_ILLEGAL) return pushed;
            ldOp = NuLoadOpFor(stOp);
            NuEmitOp(irl, NU_OP_DUP); // duplicate address : stack has A A
            NuEmitOp(irl, ldOp);      // load: stack has A origV
            NuEmitConst(irl, 1);
            NuEmitOp(irl, math);       // stack has A (newV)
            NuEmitOp(irl, NU_OP_SWAP); // stack has (newV) A
            NuEmitOp(irl, NU_OP_OVER); // stack has (newV) A (newV)
            NuEmitOp(irl, NU_OP_SWAP); // stack has (newV) (newV) A
            NuEmitOp(irl, stOp);      // stack has newV
        }
    } else {
        if (lhs) {
            pushed += NuCompileExpression(irl, lhs);
        }
        if (rhs) {
            pushed += NuCompileExpression(irl, rhs);
        }
        // could sanity check pushed here
        // assume we will push just one item
        pushed = 1;
        switch (optoken) {
        case K_NEGATE: NuEmitOp(irl, NU_OP_NEG); break;
        case K_BIT_NOT: NuEmitOp(irl, NU_OP_NOT); break;
        case K_ABS: NuEmitOp(irl, NU_OP_ABS); break;
        case '+': NuEmitOp(irl, NU_OP_ADD); break;
        case '-': NuEmitOp(irl, NU_OP_SUB); break;
        case '*': NuCompileMul(irl, node->left, node->right, 0); break;
        case K_HIGHMULT: NuCompileMul(irl, node->left, node->right, 1); break;
        case K_UNS_HIGHMULT: NuCompileMul(irl, node->left, node->right, 3); break;
        case '/': NuCompileDiv(irl, node, 0); break;
        case K_MODULUS: NuCompileDiv(irl, node, 1); break;
        case K_UNS_DIV: NuCompileDiv(irl, node, 2); break;
        case K_UNS_MOD: NuCompileDiv(irl, node, 3); break;
        case '&': NuEmitOp(irl, NU_OP_AND); break;
        case '|': NuEmitOp(irl, NU_OP_IOR); break;
        case '^': NuEmitOp(irl, NU_OP_XOR); break;
        case K_SHL: NuEmitOp(irl, NU_OP_SHL); break;
        case K_SHR: NuEmitOp(irl, NU_OP_SHR); break;
        case K_SAR: NuEmitOp(irl, NU_OP_SAR); break;
        case K_ZEROEXTEND: NuEmitOp(irl, NU_OP_ZEROX); break;
        case K_SIGNEXTEND: NuEmitOp(irl, NU_OP_SIGNX); break;
            
        case K_BOOL_NOT:
        case K_BOOL_AND:
        case K_BOOL_OR:
        case K_EQ:
        case K_NE:
        case K_LE:
        case K_GE:
        case '<':
        case '>':
        case K_LTU:
        case K_GTU:
        case K_LEU:
        case K_GEU:
        {
            NuIrLabel *skiplabel = NuCreateLabel();
            int truevalue = LangBoolIsOne(curfunc->language) ? 1 : -1;

            NuEmitConst(irl, 0);
            NuCompileBoolBranches(irl, node, NULL, skiplabel);
            NuEmitConst(irl, truevalue);
            NuEmitOp(irl, NU_OP_XOR);
            NuEmitLabel(irl, skiplabel);
        } break;
            
        default:
            ERROR(node, "Unable to handle operator 0x%x", optoken);
            break;
        }
    }
    return pushed;
}

/* returns number of longs pushed on stack */
static int
NuCompileExpression(NuIrList *irl, AST *node) {
    int pushed = 0; // number results pushed on stack
    if(!node) {
        // nothing to do here
        return pushed;
    }
    if (IsConstExpr(node)) {
        int32_t val = EvalConstExpr(node);
        NuEmitConst(irl, val);
        return 1;
    }
    switch(node->kind) {
    case AST_IDENTIFIER:
    case AST_LOCAL_IDENTIFIER:
    case AST_ARRAYREF:
    {
        AST *typ;
        int siz;
        NuIrOpcode op;
        typ = ExprType(node);
        if (!typ) {
            typ = ast_type_long;
        }
        siz = TypeSize(typ);
        op = NuCompileLhsAddress(irl, node);
        pushed = (siz+3)/4;
        if (op == NU_OP_ILLEGAL) {
            ERROR(node, "Unable to evaluate expression");
        } else {
            op = NuLoadOpFor(op);
            NuEmitOp(irl, op);
        }
    } break;
    case AST_FUNCCALL:
    {
        pushed = NuCompileFunCall(irl, node);
    } break;
    case AST_OPERATOR:
    {
        pushed = NuCompileOperator(irl, node);
    } break;
    case AST_ABSADDROF:
    {
        (void)NuCompileLhsAddress(irl, node->left); // don't care about load op, we will not use it
        pushed = 1;
    } break;
    case AST_STRINGPTR:
    {
        NuIrLabel *tmpLabel;
        NuFunData *fdata = FunData(curfunc);
        
        if (!fdata->dataLabel) {
            fdata->dataLabel = NuCreateLabel();
            flexbuf_init(&fdata->dataBuf, 32);
        }
        tmpLabel = NuIrOffsetLabel(fdata->dataLabel, flexbuf_curlen(&fdata->dataBuf));
        StringBuildBuffer(&fdata->dataBuf, node->left);
        NuEmitAddress(irl, tmpLabel);
    } break;
    case AST_CAST:
        return NuCompileExpression(irl, node->right);
    case AST_DECLARE_VAR:
        return NuCompileExpression(irl, node->right);
    case AST_SEQUENCE: {
        int n = NuCompileExpression(irl, node->left);
        NuCompileDrop(irl, n);
        return NuCompileExpression(irl, node->right);
    } break;
    case AST_ASSIGN:
        pushed = NuCompileAssign(irl, node, 1);
        break;
    default:
        ERROR(node, "Unknown expression node %d\n", node->kind);
        return 0;
    }
    return pushed;
}

static int
NuCompileExprList(NuIrList *irl, AST *node)
{
    int pushed = 0;
    while (node && node->kind == AST_EXPRLIST) {
        pushed += NuCompileExpression(irl, node->left);
        node = node->right;
    }
    if (node) {
        ERROR(node, "Expecting expression list");
    }
    return pushed;
}

static void
NuCompileStmtlist(NuIrList *irl, AST *list) {
    for (AST *ast=list; ast; ast=ast->right) {
        AST *node = ast->left;
        if (!node) {
            //ERROR(ast,"empty node?!?"); ignore, this can happen
            continue;
        }
        NuCompileStatement(irl,node);
    }
}

static void NuCompileForLoop(NuIrList *irl, AST *ast, int atleastonce) {
    AST *initstmt;
    AST *loopcond;
    AST *update;
    AST *body = 0;
    NuIrLabel *toplabel, *nextlabel, *exitlabel;
    
    initstmt = ast->left;
    ast = ast->right;
    if (!ast || ast->kind != AST_TO) {
        ERROR(ast, "Internal Error: expected AST_TO in for loop");
        return;
    }
    loopcond = ast->left;
    ast = ast->right;
    if (!ast || ast->kind != AST_STEP) {
        ERROR(ast, "Internal Error: expected AST_TO in for loop");
        return;
    }
    update = ast->left;
    ast = ast->right;
    body = ast;

    NuCompileStatement(irl, initstmt);

    toplabel = NuCreateLabel();
    nextlabel = NuCreateLabel();
    exitlabel = NuCreateLabel();
    NuPushQuitNext(exitlabel, nextlabel);
    if (!loopcond) {
        loopcond = AstInteger(1);
    }
    NuEmitLabel(irl, toplabel);
    if (!atleastonce) {
        NuCompileBoolBranches(irl, loopcond, NULL, exitlabel);
    }
    NuCompileStatement(irl, body);
    NuEmitLabel(irl, nextlabel);
    NuCompileStatement(irl, update);
    if (atleastonce) {
        NuCompileBoolBranches(irl, loopcond, toplabel, NULL);
    } else {
        NuEmitBranch(irl, NU_OP_BRA, toplabel);
    }
    NuEmitLabel(irl, exitlabel);
    NuPopQuitNext();   
}

static void NuCompileStatement(NuIrList *irl, AST *ast) {
    int n;
    NuIrLabel *toploop, *botloop, *exitloop;

    while (ast && ast->kind == AST_COMMENTEDNODE) {
        ast = ast->left;
    }
    if (!ast) return;
    switch(ast->kind) {
    case AST_COMMENT:
        // for now, do nothing
        break;
    case AST_STMTLIST:
    case AST_SEQUENCE:
        NuCompileStmtlist(irl, ast);
        break;
    case AST_ASSIGN:
        NuCompileAssign(irl, ast, 0);
        break;
    case AST_IF: {
        NuIrLabel *elselbl = NuCreateLabel();
        NuIrLabel *bottomlbl;
        AST *thenelse = ast->right;
        if (thenelse && thenelse->kind == AST_COMMENTEDNODE) thenelse = thenelse->left;
        ASSERT_AST_KIND(thenelse,AST_THENELSE,break;);
        NuCompileBoolBranches(irl, ast->left, NULL, elselbl);
        NuCompileStmtlist(irl, thenelse->left);
        if (thenelse->right) {
            bottomlbl = NuCreateLabel();
            NuEmitBranch(irl, NU_OP_BRA, elselbl);
            NuEmitLabel(irl, elselbl);
            NuCompileStmtlist(irl, thenelse->right);
        } else {
            bottomlbl = elselbl;
        }
        NuEmitLabel(irl, bottomlbl);
    } break;
        
    case AST_WHILE:
        toploop = NuCreateLabel();
        botloop = NuCreateLabel();
        NuPushQuitNext(botloop, toploop);
        NuEmitLabel(irl, toploop);
        NuCompileBoolBranches(irl, ast->left, NULL, botloop);
        NuCompileStmtlist(irl, ast->right);
        NuEmitBranch(irl, NU_OP_BRA, toploop);
        NuEmitLabel(irl, botloop);
        NuPopQuitNext();
        break;
    case AST_DOWHILE:
        toploop = NuCreateLabel();
	botloop = NuCreateLabel();
	exitloop = NuCreateLabel();
	NuPushQuitNext(exitloop, botloop);
	NuEmitLabel(irl, toploop);
        NuCompileStmtlist(irl, ast->right);
	NuEmitLabel(irl, botloop);
        NuCompileBoolBranches(irl, ast->left, toploop, NULL);
	NuEmitLabel(irl, exitloop);
	NuPopQuitNext();
	break;
    case AST_FOR:
    case AST_FORATLEASTONCE:
        NuCompileForLoop(irl, ast, ast->kind == AST_FORATLEASTONCE);
        break;
    case AST_QUITLOOP:
    case AST_ENDCASE:  /* note: in C "break" gets translated as AST_ENDCASE */
        if (!quitlabel) {
	    ERROR(ast, "loop exit statement outside of loop");
	} else {
	    NuEmitBranch(irl, NU_OP_BRA, quitlabel);
	}
        break;
    case AST_CONTINUE:
        if (!nextlabel) {
	    ERROR(ast, "loop continue statement outside of loop");
	} else {
	    NuEmitBranch(irl, NU_OP_BRA, nextlabel);
	}
	break;
    case AST_YIELD:
        /* do nothing */
        break;
    case AST_FUNCCALL:
        n = NuCompileExpression(irl, ast);
        NuCompileDrop(irl, n);
        break;
    case AST_RETURN:
        if (ast->left) {
            n = NuCompileExpression(irl, ast->left);
        } else {
            n = 0;
        }
        if (n != curfunc->numresults) {
            ERROR(ast, "number of items returned does not match function signature");
        }
        NuEmitConst(irl, n);
        NuEmitOp(irl, NU_OP_RET);
        break;
    case AST_OPERATOR:
        n = NuCompileExpression(irl, ast);
        NuCompileDrop(irl, n);
        break;
    case AST_INTEGER:
        /* do nothing */
        break;
    default:
        ERROR(ast, "Unhandled node type %d in NuCompileStatement", ast->kind);
        break;
    }
}

static void
NuCompileFunction(Function *F) {
    NuIrList *irl;

    curfunc = F;

    NuPrepareFunctionBedata(F);
    NormalizeVarOffsets(F);
    
    // I think there's other body types so let's leave this instead of using ASSERT_AST_KIND
    if (!F->body) {
        DEBUG(NULL,"compiling function %s with no body...",F->name);
    } else if (F->body->kind == AST_STRING) {
        WARNING(NULL,"compiling function %s which is just a reference",F->name);
    } else if (F->body->kind == AST_BYTECODE) {
        // do nothing
        return;
    } else if (F->body->kind != AST_STMTLIST) {
        ERROR(F->body,"Internal Error: Expected AST_STMTLIST, got id %d",F->body->kind);
        return;
    } else {
        if (F->closure) {
            // actually need variables to be in the heap
            // so insert some code to copy them
            AST *allocmem;
            AST *copymem;
            AST *args;
            int framesize = FuncLocalSize(F);
            // result = _gc_alloc_managed(framesize)
            args = NewAST(AST_EXPRLIST, AstInteger(framesize), NULL);
            allocmem = NewAST(AST_FUNCCALL, AstIdentifier("_gc_alloc_managed"), args);
            allocmem = AstAssign(AstIdentifier("result"), allocmem);
            allocmem = NewAST(AST_STMTLIST, allocmem, NULL);
            // longcopy(result, dbase, framesize)
            args = NewAST(AST_EXPRLIST, AstInteger(framesize), NULL);
            args = NewAST(AST_EXPRLIST, AstIdentifier("__interp_dbase"), args);
            args = NewAST(AST_EXPRLIST, AstIdentifier("result"), args);
            copymem = NewAST(AST_FUNCCALL, AstIdentifier("bytemove"), args);
            copymem = NewAST(AST_STMTLIST, copymem, NULL);
            // dbase := result
            args = AstAssign(AstIdentifier("__interp_vbase"), AstIdentifier("result"));
            args = NewAST(AST_STMTLIST, args, F->body);
            allocmem = AddToList(allocmem, copymem);
            allocmem = AddToList(allocmem, args);
            F->body = allocmem;
        }
        irl = &FunData(F)->irl;

        // emit function prologue
        NuEmitLabel(irl, FunData(F)->entryLabel);
        // ENTER needs on stack:
        //  number of return values
        //  number of arguments
        //  number of locals
        NuEmitConst(irl, NumRetLongs(F));
        NuEmitConst(irl, NumArgLongs(F));
        NuEmitConst(irl, NumLocalLongs(F));
        NuEmitOp(irl, NU_OP_ENTER);
        
        NuCompileStmtlist(irl, F->body);
        // emit function epilogue
        //NuEmitLabel(irl, FunData(F)->exitLabel);

        // verify that we ended with a RET
        if (irl->tail && irl->tail->op != NU_OP_RET) {
            if (NumRetLongs(F) == 0) {
                // just insert RET
                NuEmitConst(irl, 0);
                NuEmitOp(irl, NU_OP_RET);
            } else {
                ERROR(F->body, "Internal error, function does not end with RET");
            }
        }
    }
}

static void NuConvertFunctions(Module *P) {
    Module *save = current;
    Function *pf;
    current = P;

    NuPrepareModuleBedata(P);
    // Compile functions
    for (pf = P->functions; pf; pf = pf->next) {
        NuCompileFunction(pf);
    }

    current = save;
}

static void NuCompileObject(struct flexbuf *fb, Module *P) {
    Module *save = current;
    Function *pf;
    current = P;

    flexbuf_printf(fb, "'--- Object: %s\n", P->classname);
    
    /* compile DAT block */
    if (P->datblock) {
        // Got DAT block
        // TODO pass through listing data
        Flexbuf datBuf;
        Flexbuf datRelocs;

        flexbuf_init(&datBuf,2048);
        flexbuf_init(&datRelocs,1024);
        PrintDataBlock(&datBuf,P,NULL,&datRelocs);
        OutputAlignLong(&datBuf);
        NuOutputLabelNL(fb, ModData(P)->datLabel);
        OutputDataBlob(fb, &datBuf, &datRelocs, NULL);
        
        flexbuf_delete(&datRelocs);
        flexbuf_delete(&datBuf);
    }
    for (pf = P->functions; pf; pf = pf->next) {
        flexbuf_printf(fb, "'--- Function: %s\n", pf->name);
        NuOutputIrList(fb, &FunData(pf)->irl);
        if (FunData(pf)->dataLabel) {
            NuOutputLabelNL(fb, FunData(pf)->dataLabel);
            OutputDataBlob(fb, &FunData(pf)->dataBuf, NULL, NULL);
        }
    }
    current = save;
}

#ifdef NEVER
static void NuAddHeap(ByteOutputBuffer *bob, Module*P) {
    Symbol *sym = LookupSymbolInTable(&systemModule->objsyms, "__heap_base");
    Symbol *objsym = LookupSymbolInTable(&P->objsyms, "_system_");
    Label *L;

    if (! (gl_features_used & FEATURE_NEED_HEAP)) return;
    if (!objsym) return;
    if (!sym || sym->kind != SYM_LABEL) return;
    
    L = (Label *)sym->val;
    int off = L->hubval;
    //off += BCgetDAToffset(systemModule,true,NULL,false);

    sym = LookupSymbolInTable(&systemModule->objsyms, "__real_heapsize__");
    if (!sym || sym->kind != SYM_CONSTANT) return;

    uint32_t heapstart = bob->total_size + P->varsize;
    uint32_t heapsize = EvalPasmExpr((AST *)sym->val) * LONG_SIZE;

    heapsize += 4*LONG_SIZE; // reserve a slot at the end
    heapsize = (heapsize+3)&~3; // long align
    P->varsize += heapsize;

    //printf("label offset is: 0x%x heap size is %d\n", off, heapsize);

    BOB_FixupWord(bob, off, heapstart);
}
#endif

void OutputNuCode(const char *asmFileName, Module *P)
{
    FILE *asm_file;
    Module *Q;
    Module *saveCurrent = current;
    Function *saveFunc = curfunc;
    struct flexbuf asmFb;
    NuContext nuContext;
    
    NuIrInit(&nuContext);
    
    if (!P->functions) {
        ERROR(NULL, "Top level module has no functions");
        return;
    }
    // convert functions to IR
    for (Q = allparse; Q; Q = Q->next) {
        NuConvertFunctions(Q);
    }
    
    // assign opcodes to IR
    NuAssignOpcodes();

    flexbuf_init(&asmFb, 512);
    
    // find main entry point
    Function *mainFunc = GetMainFunction(P);
    if (!mainFunc) {
        ERROR(NULL,"No main function found!");
        return;
    } else if (!mainFunc->bedata) {
        ERROR(NULL,"Main function uninitialized!");
        return;
    } else if (FunData(mainFunc)->entryLabel == NULL) {
        ERROR(NULL,"Main function uncompiled!");
        return;
    }
    uint32_t clkfreq, clkmode;
    
    // set entry point and other significant variables
    if (!GetClkFreq(P, &clkfreq, &clkmode)) {
        clkfreq = 160000000;
        clkmode = 0x010007fb;
    }
    nuContext.clockFreq = clkfreq;
    nuContext.clockMode = clkmode;
    nuContext.entryPt = FunData(mainFunc)->entryLabel;
    nuContext.initSp = NuCreateLabel();
    
    // create & prepend interpreter
    NuOutputInterpreter(&asmFb, &nuContext);
    
    // compile objects to PASM
    for (Q = allparse; Q; Q = Q->next) {
        NuCompileObject(&asmFb, Q);
    }

    // finish -- heap could go here too
    NuOutputFinish(&asmFb, &nuContext);

    // output the data
    asm_file = fopen(asmFileName, "w");
    if (!asm_file) {
        ERROR(NULL, "Unable to open output file %s", asmFileName);
        return;
    }
    // emit PASM code
    flexbuf_addchar(&asmFb, 0);
    char *asmcode = flexbuf_get(&asmFb);
    fwrite(asmcode, 1, strlen(asmcode), asm_file);
    fclose(asm_file);

    current = saveCurrent;
    curfunc = saveFunc;
}
