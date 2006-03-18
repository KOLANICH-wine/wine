/*
 * IDL Compiler
 *
 * Copyright 2005 Eric Kohl
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <signal.h>

#include "widl.h"
#include "utils.h"
#include "parser.h"
#include "header.h"
#include "windef.h"

#include "widl.h"
#include "typelib.h"
#include "typelib_struct.h"
#include "typegen.h"

#define END_OF_LIST(list)       \
  do {                          \
    if (list) {                 \
      while (NEXT_LINK(list))   \
        list = NEXT_LINK(list); \
    }                           \
  } while(0)

static FILE* server;
static int indent = 0;


static int print_server(const char *format, ...)
{
    va_list va;
    int i, r;

    va_start(va, format);
    for (i = 0; i < indent; i++)
        fprintf(server, "    ");
    r = vfprintf(server, format, va);
    va_end(va);
    return r;
}


static void write_parameters_init(const func_t *func)
{
    const var_t *var;

    if (!func->args)
        return;

    var = func->args;
    while (NEXT_LINK(var)) var = NEXT_LINK(var);
    while (var)
    {
        if (var->type->type != RPC_FC_BIND_PRIMITIVE)
            print_server("%s = 0;\n", var->name);

        var = PREV_LINK(var);
    }
    fprintf(server, "\n");
}


static void write_function_stubs(type_t *iface)
{
    char *implicit_handle = get_attrp(iface->attrs, ATTR_IMPLICIT_HANDLE);
    int explicit_handle = is_attr(iface->attrs, ATTR_EXPLICIT_HANDLE);
    const func_t *func = iface->funcs;
    const var_t *var;
    const var_t* explicit_handle_var;
    unsigned int proc_offset = 0;
    unsigned int type_offset = 2;

    while (NEXT_LINK(func)) func = NEXT_LINK(func);
    while (func)
    {
        const var_t *def = func->def;
        unsigned long buffer_size = 0;
        unsigned int type_offset_func;

        /* check for a defined binding handle */
        explicit_handle_var = get_explicit_handle_var(func);
        if (explicit_handle)
        {
            if (!explicit_handle_var)
            {
                error("%s() does not define an explicit binding handle!\n", def->name);
                return;
            }
        }
        else if (implicit_handle)
        {
            if (explicit_handle_var)
            {
                error("%s() must not define a binding handle!\n", def->name);
                return;
            }
        }

        fprintf(server, "void __RPC_STUB\n");
        fprintf(server, "%s_", iface->name);
        write_name(server, def);
        fprintf(server, "(\n");
        indent++;
        print_server("PRPC_MESSAGE _pRpcMessage)\n");
        indent--;

        /* write the functions body */
        fprintf(server, "{\n");
        indent++;

        /* declare return value '_RetVal' */
        if (!is_void(def->type, NULL))
        {
            print_server("");
            write_type(server, def->type, def, def->tname);
            fprintf(server, " _RetVal;\n");
        }

        /* declare arguments */
        if (func->args)
        {
            var = func->args;
            while (NEXT_LINK(var)) var = NEXT_LINK(var);
            while (var)
            {
                print_server("");
                write_type(server, var->type, var, var->tname);
                fprintf(server, " ");
                write_name(server, var);
                write_array(server, var->array, 0);
                fprintf(server, ";\n");

                var = PREV_LINK(var);
            }
        }

        print_server("MIDL_STUB_MESSAGE _StubMsg;\n");
        print_server("RPC_STATUS _Status;\n");
        fprintf(server, "\n");


        print_server("((void)(_Status));\n");
        print_server("NdrServerInitializeNew(\n");
        indent++;
        print_server("_pRpcMessage,\n");
        print_server("&_StubMsg,\n");
        print_server("&%s_StubDesc);\n", iface->name);
        indent--;
        fprintf(server, "\n");

        write_parameters_init(func);

        if (explicit_handle_var)
        {
            print_server("%s = _pRpcMessage->Handle;\n", explicit_handle_var->name);
            fprintf(server, "\n");
        }

        print_server("RpcTryFinally\n");
        print_server("{\n");
        indent++;
        print_server("RpcTryExcept\n");
        print_server("{\n");
        indent++;

        if (func->args)
        {
            print_server("if ((_pRpcMessage->DataRepresentation & 0x0000FFFFUL) != NDR_LOCAL_DATA_REPRESENTATION)\n");
            indent++;
            print_server("NdrConvert(\n");
            indent++;
            print_server("(PMIDL_STUB_MESSAGE)&_StubMsg,\n");
            print_server("(PFORMAT_STRING)&__MIDL_ProcFormatString.Format[%u]);\n", proc_offset);
            indent -= 2;
            fprintf(server, "\n");

            /* make a copy so we don't increment the type offset twice */
            type_offset_func = type_offset;

            /* unmarshall arguments */
            write_remoting_arguments(server, indent, func, &type_offset_func, PASS_IN, PHASE_UNMARSHAL);
        }

        print_server("if (_StubMsg.Buffer > _StubMsg.BufferEnd)\n");
        print_server("{\n");
        indent++;
        print_server("RpcRaiseException(RPC_X_BAD_STUB_DATA);\n");
        indent--;
        print_server("}\n");
        indent--;
        print_server("}\n");
        print_server("RpcExcept(RPC_BAD_STUB_DATA_EXCEPTION_FILTER)\n");
        print_server("{\n");
        indent++;
        print_server("RpcRaiseException(RPC_X_BAD_STUB_DATA);\n");
        indent--;
        print_server("}\n");
        print_server("RpcEndExcept\n");
        fprintf(server, "\n");


        /* Call the real server function */
        if (!is_void(def->type, NULL))
            print_server("_RetVal = ");
        else
            print_server("");
        write_name(server, def);

        if (func->args)
        {
            int first_arg = 1;

            fprintf(server, "(\n");
            indent++;
            var = func->args;
            while (NEXT_LINK(var)) var = NEXT_LINK(var);
            while (var)
            {
                if (first_arg)
                    first_arg = 0;
                else
                    fprintf(server, ",\n");
                print_server("");
                write_name(server, var);
                var = PREV_LINK(var);
            }
            fprintf(server, ");\n");
            indent--;
        }
        else
        {
            fprintf(server, "();\n");
        }

        if (func->args)
        {
            const var_t *var = func->args;
            while (NEXT_LINK(var)) var = NEXT_LINK(var);
            while (var)
            {
                if (is_attr(var->attrs, ATTR_OUT))
                {
                    unsigned int alignment;
                    buffer_size += get_required_buffer_size(var, &alignment);
                    buffer_size += alignment;
                }
        
                var = PREV_LINK(var);
            }
        }

        if (!is_void(def->type, NULL))
        {
            unsigned int alignment;
            buffer_size += get_required_buffer_size(def, &alignment);
            buffer_size += alignment;
        }

        if (buffer_size)
        {
            fprintf(server, "\n");
            print_server("_StubMsg.BufferLength = %uU;\n", buffer_size);
            print_server("_pRpcMessage->BufferLength = _StubMsg.BufferLength;\n");
            fprintf(server, "\n");
            print_server("_Status = I_RpcGetBuffer(_pRpcMessage);\n");
            print_server("if (_Status)\n");
            indent++;
            print_server("RpcRaiseException(_Status);\n");
            indent--;
            fprintf(server, "\n");
            print_server("_StubMsg.Buffer = (unsigned char *)_pRpcMessage->Buffer;\n");
            fprintf(server, "\n");
        }

        /* marshall arguments */
        write_remoting_arguments(server, indent, func, &type_offset, PASS_OUT, PHASE_MARSHAL);

        /* marshall the return value */
        if (!is_void(def->type, NULL))
        {
            print_server("*(");
            write_type(server, def->type, def, def->tname);
            fprintf(server, " *)_StubMsg.Buffer = _RetVal;\n");
            print_server("_StubMsg.Buffer += sizeof(");
            write_type(server, def->type, def, def->tname);
            fprintf(server, ");\n");
        }

        indent--;
        print_server("}\n");
        print_server("RpcFinally\n");
        print_server("{\n");
        print_server("}\n");
        print_server("RpcEndFinally\n");

        /* calculate buffer length */
        fprintf(server, "\n");
        print_server("_pRpcMessage->BufferLength =\n");
        indent++;
        print_server("(unsigned int)(_StubMsg.Buffer - (unsigned char *)_pRpcMessage->Buffer);\n");
        indent--;
        indent--;
        fprintf(server, "}\n");
        fprintf(server, "\n");

        /* update proc_offset */
        if (func->args)
        {
            var = func->args;
            while (NEXT_LINK(var)) var = NEXT_LINK(var);
            while (var)
            {
                proc_offset += get_size_procformatstring_var(var);
                var = PREV_LINK(var);
            }
        }
        if (!is_void(def->type, NULL))
            proc_offset += get_size_procformatstring_var(def);
        else
            proc_offset += 2; /* FC_END and FC_PAD */

        func = PREV_LINK(func);
    }
}


static void write_dispatchtable(type_t *iface)
{
    unsigned long ver = get_attrv(iface->attrs, ATTR_VERSION);
    unsigned long method_count = 0;
    func_t *func = iface->funcs;

    print_server("static RPC_DISPATCH_FUNCTION %s_table[] =\n", iface->name);
    print_server("{\n");
    indent++;
    while (NEXT_LINK(func)) func = NEXT_LINK(func);
    while (func)
    {
        var_t *def = func->def;

        print_server("%s_", iface->name);
        write_name(server, def);
        fprintf(server, ",\n");

        method_count++;
        func = PREV_LINK(func);
    }
    print_server("0\n");
    indent--;
    print_server("};\n");
    print_server("RPC_DISPATCH_TABLE %s_v%d_%d_DispatchTable =\n", iface->name, LOWORD(ver), HIWORD(ver));
    print_server("{\n");
    indent++;
    print_server("%u,\n", method_count);
    print_server("%s_table\n", iface->name);
    indent--;
    print_server("};\n");
    fprintf(server, "\n");
}


static void write_stubdescdecl(type_t *iface)
{
    print_server("extern const MIDL_STUB_DESC %s_StubDesc;\n", iface->name);
    fprintf(server, "\n");
}


static void write_stubdescriptor(type_t *iface, int expr_eval_routines)
{
    print_server("static const MIDL_STUB_DESC %s_StubDesc =\n", iface->name);
    print_server("{\n");
    indent++;
    print_server("(void *)& %s___RpcServerInterface,\n", iface->name);
    print_server("MIDL_user_allocate,\n");
    print_server("MIDL_user_free,\n");
    print_server("0,\n");
    print_server("0,\n");
    print_server("0,\n");
    if (expr_eval_routines)
        print_server("ExprEvalRoutines,\n");
    else
        print_server("0,\n");
    print_server("0,\n");
    print_server("__MIDL_TypeFormatString.Format,\n");
    print_server("1, /* -error bounds_check flag */\n");
    print_server("0x10001, /* Ndr library version */\n");
    print_server("0,\n");
    print_server("0x50100a4, /* MIDL Version 5.1.164 */\n");
    print_server("0,\n");
    print_server("0,\n");
    print_server("0,  /* notify & notify_flag routine table */\n");
    print_server("1,  /* Flags */\n");
    print_server("0,  /* Reserved3 */\n");
    print_server("0,  /* Reserved4 */\n");
    print_server("0   /* Reserved5 */\n");
    indent--;
    print_server("};\n");
    fprintf(server, "\n");
}


static void write_serverinterfacedecl(type_t *iface)
{
    unsigned long ver = get_attrv(iface->attrs, ATTR_VERSION);
    UUID *uuid = get_attrp(iface->attrs, ATTR_UUID);

    print_server("extern RPC_DISPATCH_TABLE %s_v%d_%d_DispatchTable;\n", iface->name, LOWORD(ver), HIWORD(ver));
    fprintf(server, "\n");
    print_server("static const RPC_SERVER_INTERFACE %s___RpcServerInterface =\n", iface->name );
    print_server("{\n");
    indent++;
    print_server("sizeof(RPC_SERVER_INTERFACE),\n");
    print_server("{{0x%08lx,0x%04x,0x%04x,{0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x}},{%d,%d}},\n",
                 uuid->Data1, uuid->Data2, uuid->Data3, uuid->Data4[0], uuid->Data4[1],
                 uuid->Data4[2], uuid->Data4[3], uuid->Data4[4], uuid->Data4[5], uuid->Data4[6],
                 uuid->Data4[7], LOWORD(ver), HIWORD(ver));
    print_server("{{0x8a885d04,0x1ceb,0x11c9,{0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60}},{2,0}},\n"); /* FIXME */
    print_server("&%s_v%d_%d_DispatchTable,\n", iface->name, LOWORD(ver), HIWORD(ver));
    print_server("0,\n");
    print_server("0,\n");
    print_server("0,\n");
    print_server("0,\n");
    print_server("0,\n");
    indent--;
    print_server("};\n");
    print_server("RPC_IF_HANDLE %s_v%d_%d_s_ifspec = (RPC_IF_HANDLE)& %s___RpcServerInterface;\n",
                 iface->name, LOWORD(ver), HIWORD(ver), iface->name);
    fprintf(server, "\n");
}

static void write_formatdesc( const char *str )
{
    print_server("typedef struct _MIDL_%s_FORMAT_STRING\n", str );
    print_server("{\n");
    indent++;
    print_server("short Pad;\n");
    print_server("unsigned char Format[%s_FORMAT_STRING_SIZE];\n", str);
    indent--;
    print_server("} MIDL_%s_FORMAT_STRING;\n", str);
    print_server("\n");
}


static void write_formatstringsdecl(type_t *iface)
{
    int byte_count = 1;

    print_server("#define TYPE_FORMAT_STRING_SIZE %d\n", 3); /* FIXME */

    /* determine the proc format string size */
    if (iface->funcs)
    {
        func_t *func = iface->funcs;
        while (NEXT_LINK(func)) func = NEXT_LINK(func);
        while (func)
        {
            /* argument list size */
            if (func->args)
            {
                var_t *var = func->args;
                while (NEXT_LINK(var)) var = NEXT_LINK(var);
                while (var)
                {
                    byte_count += 2; /* FIXME: determine real size */
                    var = PREV_LINK(var);
                }
            }
    
            /* return value size */
            byte_count += 2; /* FIXME: determine real size */
            func = PREV_LINK(func);
        }
    }
    print_server("#define PROC_FORMAT_STRING_SIZE %d\n", byte_count);

    fprintf(server, "\n");
    write_formatdesc("TYPE");
    write_formatdesc("PROC");
    fprintf(server, "\n");
    print_server("extern const MIDL_TYPE_FORMAT_STRING __MIDL_TypeFormatString;\n");
    print_server("extern const MIDL_PROC_FORMAT_STRING __MIDL_ProcFormatString;\n");
    print_server("\n");
}


static void init_server(void)
{
    if (server)
        return;
    if (!(server = fopen(server_name, "w")))
        error("Could not open %s for output\n", server_name);

    print_server("/*** Autogenerated by WIDL %s from %s - Do not edit ***/\n", WIDL_FULLVERSION, input_name);
    print_server("#include <string.h>\n");
    fprintf(server, "\n");
    print_server("#include \"%s\"\n", header_name);
    fprintf(server, "\n");
}


void write_server(ifref_t *ifaces)
{
    ifref_t *iface = ifaces;

    if (!do_server)
        return;
    if (!iface)
        return;
    END_OF_LIST(iface);

    init_server();
    if (!server)
        return;

    while (iface)
    {
        fprintf(server, "/*****************************************************************************\n");
        fprintf(server, " * %s interface\n", iface->iface->name);
        fprintf(server, " */\n");
        fprintf(server, "\n");

        if (iface->iface->funcs)
        {
            int expr_eval_routines;

            write_formatstringsdecl(iface->iface);
            write_serverinterfacedecl(iface->iface);
            write_stubdescdecl(iface->iface);
    
            write_function_stubs(iface->iface);
    
            print_server("#if !defined(__RPC_WIN32__)\n");
            print_server("#error  Invalid build platform for this stub.\n");
            print_server("#endif\n");
            fprintf(server, "\n");

            write_procformatstring(server, iface->iface);
            write_typeformatstring(server, iface->iface);

            fprintf(server, "\n");

            expr_eval_routines = write_expr_eval_routines(server, iface->iface->name);
            if (expr_eval_routines)
                write_expr_eval_routine_list(server, iface->iface->name);

            write_stubdescriptor(iface->iface, expr_eval_routines);
            write_dispatchtable(iface->iface);
        }

        iface = PREV_LINK(iface);
    }

    fclose(server);
}
