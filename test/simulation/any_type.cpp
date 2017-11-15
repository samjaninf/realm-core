/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include "any_type.hpp"

using namespace realm;
using namespace realm::simulation;

AnyType::AnyType() noexcept
{
    m_type = type_Int;
    m_int = 0;
}

AnyType::~AnyType() noexcept
{
}

AnyType::AnyType(bool value) noexcept
{
    m_type = type_Bool;
    m_bool = value;
}

AnyType::AnyType(int64_t value) noexcept
{
    m_type = type_Int;
    m_int = value;
}

AnyType::AnyType(float value) noexcept
{
    m_type = type_Float;
    m_float = value;
}

AnyType::AnyType(double value) noexcept
{
    m_type = type_Double;
    m_double = value;
}

AnyType::AnyType(DataType type) noexcept
{
    m_type = type;
    m_int = 0;
}

AnyType::AnyType(StringData value) noexcept
{
    m_type = type_String;
    m_data = std::string(value); // deep copy
}

AnyType::AnyType(BinaryData value) noexcept
{
    m_type = type_Binary;
    m_data = std::string(value); // deep copy
}

AnyType::AnyType(Timestamp value) noexcept
{
    m_type = type_Timestamp;
    m_timestamp = value;
}

AnyType::AnyType(StableLink value) noexcept
{
    m_type = type_Link;
    m_link = value;
}

DataType AnyType::get_type() const noexcept
{
    return m_type;
}

int64_t AnyType::get_int() const noexcept
{
    REALM_ASSERT(m_type == type_Int);
    return m_int;
}

bool AnyType::get_bool() const noexcept
{
    REALM_ASSERT(m_type == type_Bool);
    return m_bool;
}

float AnyType::get_float() const noexcept
{
    REALM_ASSERT(m_type == type_Float);
    return m_float;
}

double AnyType::get_double() const noexcept
{
    REALM_ASSERT(m_type == type_Double);
    return m_double;
}

StringData AnyType::get_string() const noexcept
{
    REALM_ASSERT(m_type == type_String);
    return StringData(m_data);
}

BinaryData AnyType::get_binary() const noexcept
{
    REALM_ASSERT(m_type == type_Binary);
    return BinaryData(m_data);
}

Timestamp AnyType::get_timestamp() const noexcept
{
    REALM_ASSERT(m_type == type_Timestamp);
    return m_timestamp;
}

StableLink AnyType::get_link() const noexcept
{
    REALM_ASSERT(m_type == type_Link);
    return m_link;
}

void AnyType::add_int(int64_t value) noexcept
{
    REALM_ASSERT(m_type == type_Int);
    m_int += value;
}

void AnyType::set_int(int64_t value) noexcept
{
    m_type = type_Int;
    m_int = value;
}

void AnyType::set_bool(bool value) noexcept
{
    m_type = type_Bool;
    m_bool = value;
}

void AnyType::set_float(float value) noexcept
{
    m_type = type_Float;
    m_float = value;
}

void AnyType::set_double(double value) noexcept
{
    m_type = type_Double;
    m_double = value;
}

void AnyType::set_string(StringData value) noexcept
{
    m_type = type_String;
    m_data = std::string(value);
}

void AnyType::set_binary(BinaryData value) noexcept
{
    m_type = type_Binary;
    m_data = std::string(value);
}

void AnyType::set_binary(const char* data, size_t size) noexcept
{
    m_type = type_Binary;
    m_data = std::string(data, size);
}

void AnyType::set_timestamp(Timestamp value) noexcept
{
    m_type = type_Timestamp;
    m_timestamp = value;
}

void AnyType::set_link(StableLink value) noexcept
{
    m_type = type_Link;
    m_link = value;
}

std::vector<AnyType>& AnyType::get_list() noexcept
{
    return m_list;
}

AnyType AnyType::get_default_value(DataType type)
{
    switch (type) {
        case realm::type_Bool:
            return AnyType(false);
        case realm::type_Int:
            return AnyType(int64_t(0));
        case realm::type_Float:
            return AnyType(float(0));
        case realm::type_Double:
            return AnyType(double(0));
        case realm::type_Binary:
            return AnyType(BinaryData());
        case realm::type_String:
            return AnyType(StringData());
        case realm::type_Timestamp:
            return AnyType(Timestamp());
        case realm::type_Table:
            return AnyType(type_Table);
        case realm::type_Mixed:
            return AnyType(int64_t(0));
        case realm::type_OldDateTime:
            REALM_ASSERT(false);
        case realm::type_Link:
            return AnyType(StableLink{});
        case realm::type_LinkList:
            return AnyType(type_LinkList);
    }
}
