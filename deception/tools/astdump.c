/* The AST oracle: injected into the original and the hybrid compilers in front of CodeGen_Generator, where the
 * frontend hands each function's statements to the backend. Writes them, canonically (no pointers), to the file
 * the environment variable MWCC_AST_DUMP names. */
#include <stdio.h>
#include <stdlib.h>
#include "compiler/common.h"
#include "compiler/CFunc.h"
#include "compiler/enode.h"
#include "compiler/objects.h"
#include "compiler/types.h"

extern void CodeGen_Generator_Original(Statement *statements, Object *functionObject, Boolean context, Boolean zero);
extern int atexit(void (*function)(void));

#define MAX_LABELS 4096
#define MAX_HITS 8192
#define MAX_DEPTH 256

static FILE *dump_file;
static int dump_opened;
static void *labels[MAX_LABELS];
static int label_count;
static unsigned long hits[MAX_HITS];
static int hits_registered;

static int label_id(void *label) {
    int i;
    if (label == NULL)
        return -1;
    for (i = 0; i < label_count; i++) {
        if (labels[i] == label)
            return i;
    }
    if (label_count < MAX_LABELS)
        labels[label_count++] = label;
    return label_count - 1;
}

static void indent(int depth) {
    int i;
    for (i = 0; i < depth; i++)
        fputc(' ', dump_file);
}

static void put_name(HashNameNode *name) {
    fprintf(dump_file, "%s", name ? name->name : "<anon>");
}

static void put_type(Type *type, int depth) {
    if (type == NULL) {
        fprintf(dump_file, "<null>");
        return;
    }
    if (depth > 8) {
        fprintf(dump_file, "...");
        return;
    }
    switch (type->type) {
        case TYPEPOINTER:
            fprintf(dump_file, "ptr(q%lx,", (unsigned long)((TypePointer *)type)->qual);
            put_type(((TypePointer *)type)->target, depth + 1);
            fprintf(dump_file, ")");
            break;
        case TYPEARRAY:
            fprintf(dump_file, "array%ld(", (long)type->size);
            put_type(((TypePointer *)type)->target, depth + 1);
            fprintf(dump_file, ")");
            break;
        case TYPESTRUCT:
            fprintf(dump_file, "struct%ld:", (long)type->size);
            put_name(((TypeStruct *)type)->name);
            break;
        case TYPECLASS:
            fprintf(dump_file, "class%ld:", (long)type->size);
            put_name(((TypeClass *)type)->classname);
            break;
        case TYPEENUM:
            fprintf(dump_file, "enum%ld:", (long)type->size);
            put_name(((TypeEnum *)type)->enumname);
            break;
        case TYPEFUNC:
            fprintf(dump_file, "func(f%lx,", (unsigned long)((TypeFunc *)type)->flags);
            put_type(((TypeFunc *)type)->functype, depth + 1);
            fprintf(dump_file, ")");
            break;
        case TYPEBITFIELD:
            fprintf(dump_file, "bits%d@%d(", ((TypeBitfield *)type)->bitlength, ((TypeBitfield *)type)->offset);
            put_type(((TypeBitfield *)type)->bitfieldtype, depth + 1);
            fprintf(dump_file, ")");
            break;
        default:
            fprintf(dump_file, "t%d:%ld", type->type, (long)type->size);
            break;
    }
}

static void put_object(Object *object) {
    if (object == NULL) {
        fprintf(dump_file, "<null>");
        return;
    }
    put_name(object->name);
    fprintf(dump_file, "{o%d s%d d%d q%lx f%x ", object->otype, object->sclass, object->datatype,
            (unsigned long)object->qual, object->flags);
    put_type(object->type, 0);
    fprintf(dump_file, "}");
}

static void put_node(ENode *node, int depth);

static void put_child(ENode *node, int depth) {
    fputc('\n', dump_file);
    put_node(node, depth);
}

static void put_node(ENode *node, int depth) {
    ENodeList *list;
    unsigned char *bytes;
    int i;

    indent(depth);
    if (node == NULL) {
        fprintf(dump_file, "<null>");
        return;
    }
    if (depth > MAX_DEPTH) {
        fprintf(dump_file, "<too deep>");
        return;
    }
    /* (ignored and hascall are left uninitialized on some nodes: not written) */
    fprintf(dump_file, "E%d f%x c%d ", node->type, node->flags, node->cost);
    put_type(node->rtype, 0);
    switch (node->type) {
        case EPOSTINC: case EPOSTDEC: case EPREINC: case EPREDEC: case EINDIRECT: case EMONMIN: case EBINNOT:
        case ELOGNOT: case EFORCELOAD: case ETYPCON: case EBITFIELD:
            put_child(node->data.monadic, depth + 1);
            break;
        case EMUL: case EMULV: case EDIV: case EMODULO: case EADDV: case ESUBV: case EADD: case ESUB: case ESHL:
        case ESHR: case ELESS: case EGREATER: case ELESSEQU: case EGREATEREQU: case EEQU: case ENOTEQU: case EAND:
        case EXOR: case EOR: case ELAND: case ELOR: case EASS: case EMULASS: case EDIVASS: case EMODASS:
        case EADDASS: case ESUBASS: case ESHLASS: case ESHRASS: case EANDASS: case EXORASS: case EORASS:
        case ECOMMA: case EPMODULO: case EROTL: case EROTR: case EBCLR: case EBTST: case EBSET:
            put_child(node->data.diadic.left, depth + 1);
            put_child(node->data.diadic.right, depth + 1);
            break;
        case EINTCONST:
            fprintf(dump_file, " = %08lx%08lx", (unsigned long)node->data.intval.hi,
                    (unsigned long)node->data.intval.lo);
            break;
        case EFLOATCONST:
            bytes = (unsigned char *)&node->data.floatval;
            fprintf(dump_file, " = ");
            for (i = 0; i < 8; i++)
                fprintf(dump_file, "%02x", bytes[i]);
            break;
        case ESTRINGCONST:
            fprintf(dump_file, " size %ld x%d \"", (long)node->data.string.size, node->data.string.useExplicitSize);
            for (i = 0; i < node->data.string.size && i < 256; i++)
                fprintf(dump_file, "%02x", (unsigned char)node->data.string.data[i]);
            fprintf(dump_file, "\"");
            break;
        case ECOND:
            put_child(node->data.cond.cond, depth + 1);
            put_child(node->data.cond.expr1, depth + 1);
            put_child(node->data.cond.expr2, depth + 1);
            break;
        case EFUNCCALL: case EFUNCCALLP:
            fprintf(dump_file, " functype ");
            put_type((Type *)node->data.funccall.functype, 0);
            put_child(node->data.funccall.funcref, depth + 1);
            for (list = node->data.funccall.args; list != NULL; list = list->next)
                put_child(list->node, depth + 1);
            break;
        case EOBJREF:
            fprintf(dump_file, " ");
            put_object(node->data.objref);
            break;
        case ETEMP:
            fprintf(dump_file, " temp %ld d%d ", (long)node->data.temp.uniqueid, node->data.temp.needs_dtor);
            put_type(node->data.temp.type, 0);
            break;
        case EPRECOMP:
            fprintf(dump_file, " precomp %ld", (long)node->data.longval);
            break;
        case ELABEL:
            fprintf(dump_file, " L%d", label_id(node->data.labelAddress));
            break;
        default:
            /* (a kind this oracle does not walk yet: its kind and type above, and a marker to find it by) */
            fprintf(dump_file, " <unwalked>");
            break;
    }
}

static void dump_statements(Statement *statements, Object *function) {
    Statement *statement;
    int index;

    label_count = 0;
    fprintf(dump_file, "FUNCTION ");
    put_object(function);
    fprintf(dump_file, "\n");
    for (statement = statements, index = 0; statement != NULL; statement = statement->next, index++) {
        fprintf(dump_file, "S%d k%d f%x v%d @%ld", index, statement->type, statement->flags, statement->value,
                (long)statement->sourceoffset);
        switch (statement->type) {
            case ST_LABEL: case ST_GOTO:
                fprintf(dump_file, " L%d", label_id(statement->target.label));
                break;
            case ST_IFGOTO: case ST_IFNGOTO:
                fprintf(dump_file, " L%d\n", label_id(statement->target.label));
                put_node(statement->expr.expression, 2);
                break;
            case ST_EXPRESSION: case ST_RETURN: case ST_SWITCH: case ST_GOTOEXPR: case ST_BEGINCATCH:
            case ST_ENDCATCHDTOR:
                fprintf(dump_file, "\n");
                put_node(statement->expr.expression, 2);
                break;
            default:
                break;
        }
        fprintf(dump_file, "\n");
    }
    fprintf(dump_file, "END\n");
    fflush(dump_file);
}

static void write_hits(void) {
    char *path;
    FILE *file;
    int i;

    path = getenv("MWCC_HITS");
    if (path == NULL || (file = fopen(path, "w")) == NULL)
        return;
    for (i = 0; i < MAX_HITS; i++) {
        if (hits[i])
            fprintf(file, "%d %lu\n", i, hits[i]);
    }
    fclose(file);
}

/* The hybrid's entry stub of injected function INDEX calls this first: the calls are written at exit to the file
 * the environment variable MWCC_HITS names, as "index count" lines. */
void AstDump_Hit(int index) {
    if (!hits_registered) {
        hits_registered = 1;
        atexit(write_hits);
    }
    if (index >= 0 && index < MAX_HITS)
        hits[index]++;
}

void AstDump_CodeGen_Generator(Statement *statements, Object *functionObject, Boolean context, Boolean zero) {
    char *path;

    if (!dump_opened) {
        dump_opened = 1;
        path = getenv("MWCC_AST_DUMP");
        dump_file = path ? fopen(path, "a") : NULL;
    }
    if (dump_file != NULL)
        dump_statements(statements, functionObject);
    CodeGen_Generator_Original(statements, functionObject, context, zero);
}
