//===-- ValueObjectMemory.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "lldb/Core/ValueObjectMemory.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/Module.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"

#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"


using namespace lldb_private;

ValueObjectMemory::ValueObjectMemory (ExecutionContextScope *exe_scope,
                                      const char *name, 
                                      const Address &address,
                                      lldb::TypeSP &type_sp) :
    ValueObject(exe_scope),
    m_address (address),
    m_type_sp(type_sp)
{
    // Do not attempt to construct one of these objects with no variable!
    assert (m_type_sp.get() != NULL);
    SetName (name);
    m_value.SetContext(Value::eContextTypeLLDBType, m_type_sp.get());
    lldb::addr_t load_address = m_address.GetLoadAddress(m_update_point.GetTarget());
    if (load_address != LLDB_INVALID_ADDRESS)
    {
        m_value.SetValueType(Value::eValueTypeLoadAddress);
        m_value.GetScalar() = load_address;
    }
    else
    {
        lldb::addr_t file_address = m_address.GetFileAddress();
        if (file_address != LLDB_INVALID_ADDRESS)
        {
            m_value.SetValueType(Value::eValueTypeFileAddress);
            m_value.GetScalar() = file_address;
        }
        else
        {
            m_value.GetScalar() = m_address.GetOffset();
            m_value.SetValueType (Value::eValueTypeScalar);
        }
    }
}

ValueObjectMemory::~ValueObjectMemory()
{
}

lldb::clang_type_t
ValueObjectMemory::GetClangType ()
{
    return m_type_sp->GetClangForwardType();
}

ConstString
ValueObjectMemory::GetTypeName()
{
    return m_type_sp->GetName();
}

uint32_t
ValueObjectMemory::CalculateNumChildren()
{
    return m_type_sp->GetNumChildren(true);
}

clang::ASTContext *
ValueObjectMemory::GetClangAST ()
{
    return m_type_sp->GetClangAST();
}

size_t
ValueObjectMemory::GetByteSize()
{
    return m_type_sp->GetByteSize();
}

lldb::ValueType
ValueObjectMemory::GetValueType() const
{
    // RETHINK: Should this be inherited from somewhere?
    return lldb::eValueTypeVariableGlobal;
}

bool
ValueObjectMemory::UpdateValue ()
{
    SetValueIsValid (false);
    m_error.Clear();

    ExecutionContext exe_ctx (GetExecutionContextScope());
    
    if (exe_ctx.target)
    {
        m_data.SetByteOrder(exe_ctx.target->GetArchitecture().GetByteOrder());
        m_data.SetAddressByteSize(exe_ctx.target->GetArchitecture().GetAddressByteSize());
    }

    Value old_value(m_value);
    if (m_address.IsValid())
    {
        Value::ValueType value_type = m_value.GetValueType();

        switch (value_type)
        {
        default:
            assert(!"Unhandled expression result value kind...");
            break;

        case Value::eValueTypeScalar:
            // The variable value is in the Scalar value inside the m_value.
            // We can point our m_data right to it.
            m_error = m_value.GetValueAsData (&exe_ctx, GetClangAST(), m_data, 0);
            break;

        case Value::eValueTypeFileAddress:
        case Value::eValueTypeLoadAddress:
        case Value::eValueTypeHostAddress:
            // The DWARF expression result was an address in the inferior
            // process. If this variable is an aggregate type, we just need
            // the address as the main value as all child variable objects
            // will rely upon this location and add an offset and then read
            // their own values as needed. If this variable is a simple
            // type, we read all data for it into m_data.
            // Make sure this type has a value before we try and read it

            // If we have a file address, convert it to a load address if we can.
            if (value_type == Value::eValueTypeFileAddress && exe_ctx.process)
            {
                lldb::addr_t load_addr = m_address.GetLoadAddress(exe_ctx.target);
                if (load_addr != LLDB_INVALID_ADDRESS)
                {
                    m_value.SetValueType(Value::eValueTypeLoadAddress);
                    m_value.GetScalar() = load_addr;
                }
            }

            if (ClangASTContext::IsAggregateType (GetClangType()))
            {
                // this value object represents an aggregate type whose
                // children have values, but this object does not. So we
                // say we are changed if our location has changed.
                SetValueDidChange (value_type != old_value.GetValueType() || m_value.GetScalar() != old_value.GetScalar());
            }
            else
            {
                // Copy the Value and set the context to use our Variable
                // so it can extract read its value into m_data appropriately
                Value value(m_value);
                value.SetContext(Value::eContextTypeLLDBType, m_type_sp.get());
                m_error = value.GetValueAsData(&exe_ctx, GetClangAST(), m_data, 0);
            }
            break;
        }

        SetValueIsValid (m_error.Success());
    }
    return m_error.Success();
}



bool
ValueObjectMemory::IsInScope ()
{
    // FIXME: Maybe try to read the memory address, and if that works, then
    // we are in scope?
    return true;
}

