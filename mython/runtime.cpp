#include "runtime.h"

#include <cassert>
#include <optional>
#include <sstream>

using namespace std;

namespace runtime
{

ObjectHolder::ObjectHolder(std::shared_ptr<Object> data)
    : data_(std::move(data))
{}

void ObjectHolder::AssertIsValid() const
{
    assert(data_ != nullptr);
}

ObjectHolder ObjectHolder::Share(Object& object)
{
    // Возвращаем невладеющий shared_ptr (его deleter ничего не делает)
    return ObjectHolder(std::shared_ptr<Object>(&object, [](auto* /*p*/)
                                                { /* do nothing */
                                                }));
}

ObjectHolder ObjectHolder::None()
{
    return ObjectHolder();
}

Object& ObjectHolder::operator*() const
{
    AssertIsValid();
    return *Get();
}

Object* ObjectHolder::operator->() const
{
    AssertIsValid();
    return Get();
}

// Возвращает указатель shared_ptr на хранимые данные
Object* ObjectHolder::Get() const
{
    return data_.get();
}

// ==true, если внутренний shared_ptr не равен nullptr
ObjectHolder::operator bool() const
{
    return Get() != nullptr;
}

bool IsTrue(const ObjectHolder& object)
{
    // Сначала проверим саму ссылку
    if (!object)
    {
        return false;
    }

    // Проверяем Value-типы. У нас это наследники класса ValueObject<T>
    if (((object.TryAs<Number>() != nullptr) && (object.TryAs<Number>()->GetValue() != 0))    // если object это Number и не ноль
        || ((object.TryAs<Bool>() != nullptr) && (object.TryAs<Bool>()->GetValue() == true))    //  если object это Bool и == true
        || ((object.TryAs<String>() != nullptr) && (object.TryAs<String>()->GetValue().size() != 0)))   // если object - не пустая строка
    {
        return true;
    }

    // Во всех остальных случаях возвращаем false
    return false;
}

void ClassInstance::Print(std::ostream& os, Context& context)
{
    // Если есть метод __str__ у объекта, использум его
    if (this->HasMethod("__str__"s, 0))
    {
        // Получем ObjectHolder* и вызываем его Print в зависимости от типа значения
        this->Call("__str__"s, {}, context)->Print(os, context);
    }
    else
    {
        // Выводим просто адрес объекта
        os << this;
    }
}

bool ClassInstance::HasMethod(const std::string& method, size_t argument_count) const
{
    // Проверяем, есть ли в таблице виртуальных методов метод с именем method
    auto method_ptr = cls_.GetMethod(method);
    if (method_ptr != nullptr)
    {
        // Проверяем совпадение числа аргументов
        if (method_ptr->formal_params.size() == argument_count)
        {
            return true;
        }
    }
    
    return false;
}

Closure& ClassInstance::Fields()
{
    return fields_;
}

const Closure& ClassInstance::Fields() const
{
    return fields_;
}

ClassInstance::ClassInstance(const Class& cls)
    :cls_(cls)
{}

ObjectHolder ClassInstance::Call(const std::string& method,
                                 const std::vector<ObjectHolder>& actual_args,
                                 Context& context)
{
    if (this->HasMethod(method, actual_args.size()))
    {
        // Неявный параметр всех методов в Mython — специальный 
        // параметр self, аналог указателя this в C++. Параметр self 
        // ссылается на текущий объект класса.
        // Поля не объявляются заранее, а добавляются в объект класса 
        // при первом присваивании. Поэтому обращения к полям класса всегда
        // надо начинать с self., чтобы отличать их от локальных переменных.
        // Share() применяется для передачи self при вызове методов.
        Closure closure = { {"self", ObjectHolder::Share(*this)} };

        // Получаем указатель на метод из таблицы виртуальных функций
        auto method_ptr = cls_.GetMethod(method);

        // По очереди читаем имена формальных параметров и сопоставляем
        // им очередной фактический аргумент из парамеров.
        // Получаем запись словаря Closure вида "имя_параметра = значение_параметра"
        for (size_t i = 0; i < method_ptr->formal_params.size(); ++i)
        {
            std::string arg = method_ptr->formal_params[i];
            closure[arg] = actual_args[i];
        }
        // Итого локальная closure содержит все аргументы плюс ссылка на self для доступа к fields_

        return method_ptr->body->Execute(closure, context);
    }
    else
    {
        throw std::runtime_error("Call for a not defined method"s);
    }
}

Class::Class(std::string name, std::vector<Method> methods, const Class* parent)
    : name_(std::move(name)), methods_(std::move(methods)), parent_(std::move(parent))
{
    // Реализуем механизм виртуальных функций для Mython классов

    // Сначала запомним в таблице виртуальных функций методы родителя, если он есть
    if (parent_ != nullptr)
    {
        for (const auto& parent_method : parent_->methods_)
        {
            vftable_[parent_method.name] = &parent_method;
        }
    }

    // Теперь поместим в таблицу виртуальных функций собственные методы класса.
    // Если таблица уже содержит методы родителя, методы с одинаковыми именами
    // будут перезаписаны адресами дочерних методов.
    for (const auto& method : methods_)
    {
        vftable_[method.name] = &method;
    }
}

const Method* Class::GetMethod(const std::string& name) const
{
    // Ищем метод в виртуальной таблице
    if (vftable_.count(name) != 0)
    {
        return vftable_.at(name);
    }

    // Такого метода в виртуальной таблице нет
    return nullptr;
}

//[[nodiscard]] inline const std::string& Class::GetName() const
[[nodiscard]] const std::string& Class::GetName() const
{
    return name_;
}

void Class::Print(ostream& os, [[maybe_unused]] Context& context)
{
    //os << "Class "sv << this->GetName();
    os << "Class "sv << GetName();
}

void Bool::Print(std::ostream& os, [[maybe_unused]] Context& context)
{
    os << (GetValue() ? "True"sv : "False"sv);
}


//////////////////////////////////////////////
// Функции сравнения объектов Mython программы

bool Equal(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context)
{
    // 1. Если lhs и rhs имеют значение None, функция возвращает true.
    if (!lhs && !rhs)
    {
        return true;
    }

    // 2. Возвращает true, если lhs и rhs содержат одинаковые числа, строки или значения типа Bool.
    {
        auto lhs_ptr = lhs.TryAs<Number>();
        auto rhs_ptr = rhs.TryAs<Number>();
        if ((lhs_ptr != nullptr) && (rhs_ptr != nullptr))
        {
            return lhs_ptr->GetValue() == rhs_ptr->GetValue();
        }
        //else разные типы, ничего не делаем, проверяем дальше
    }

    {
        auto lhs_ptr = lhs.TryAs<String>();
        auto rhs_ptr = rhs.TryAs<String>();
        if ((lhs_ptr != nullptr) && (rhs_ptr != nullptr))
        {
            return lhs_ptr->GetValue() == rhs_ptr->GetValue();
        }
        //else разные типы, ничего не делаем, проверяем дальше
    }

    {
        auto lhs_ptr = lhs.TryAs<Bool>();
        auto rhs_ptr = rhs.TryAs<Bool>();
        if ((lhs_ptr != nullptr) && (rhs_ptr != nullptr))
        {
            return lhs_ptr->GetValue() == rhs_ptr->GetValue();
        }
        //else разные типы, ничего не делаем, проверяем дальше
    }

    // 3. Если lhs - объект с методом __eq__, функция возвращает результат 
    //    вызова lhs.__eq__(rhs), приведённый к типу Bool.
    {
        auto lhs_ptr = lhs.TryAs<ClassInstance>();
        if (lhs_ptr != nullptr)
        {
            if (lhs_ptr->HasMethod("__eq__"s, 1))
            {
                ObjectHolder result = lhs_ptr->Call("__eq__"s, { rhs }, context);
                return result.TryAs<Bool>()->GetValue();
            }
        }
    }

    // 4. В остальных случаях функция выбрасывает исключение runtime_error.
    throw std::runtime_error("Cannot compare objects for equality"s);
}


bool Less(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context)
{
    // 1. Если lhs и rhs - числа, строки или значения bool, функция
    // возвращает результат их сравнения оператором <
    {
        auto lhs_ptr = lhs.TryAs<Number>();
        auto rhs_ptr = rhs.TryAs<Number>();
        if ((lhs_ptr != nullptr) && (rhs_ptr != nullptr))
        {
            return lhs_ptr->GetValue() < rhs_ptr->GetValue();
        }
        //else разные типы, ничего не делаем, проверяем дальше
    }

    {
        auto lhs_ptr = lhs.TryAs<String>();
        auto rhs_ptr = rhs.TryAs<String>();
        if ((lhs_ptr != nullptr) && (rhs_ptr != nullptr))
        {
            return lhs_ptr->GetValue() < rhs_ptr->GetValue();
        }
        //else разные типы, ничего не делаем, проверяем дальше
    }

    {
        auto lhs_ptr = lhs.TryAs<Bool>();
        auto rhs_ptr = rhs.TryAs<Bool>();
        if ((lhs_ptr != nullptr) && (rhs_ptr != nullptr))
        {
            return lhs_ptr->GetValue() < rhs_ptr->GetValue();
        }
        //else разные типы, ничего не делаем, проверяем дальше
    }

    // 2. Если lhs - объект с методом __lt__, возвращает результат
    //  вызова lhs.__lt__(rhs), приведённый к типу bool
    {
        auto lhs_ptr = lhs.TryAs<ClassInstance>();
        if (lhs_ptr != nullptr)
        {
            if (lhs_ptr->HasMethod("__lt__"s, 1))
            {
                ObjectHolder result = lhs_ptr->Call("__lt__"s, { rhs }, context);
                return result.TryAs<Bool>()->GetValue();
            }
        }
    }

    // 3. В остальных случаях функция выбрасывает исключение runtime_error.
    throw std::runtime_error("Cannot compare objects for less"s);
}

bool NotEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context)
{
    return !Equal(lhs, rhs, context);
}

bool Greater(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context)
{
    return !(Less(lhs, rhs, context) || Equal(lhs, rhs, context));
}

bool LessOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context)
{
    return Less(lhs, rhs, context) || Equal(lhs, rhs, context);
}

bool GreaterOrEqual(const ObjectHolder& lhs, const ObjectHolder& rhs, Context& context)
{
    return !Less(lhs, rhs, context);
}

}  // namespace runtime
