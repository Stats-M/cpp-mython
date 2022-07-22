#include "statement.h"

#include <iostream>
#include <sstream>

using namespace std;

namespace ast
{

using runtime::Closure;
using runtime::Context;
using runtime::ObjectHolder;

namespace
{
const string ADD_METHOD = "__add__"s;
const string INIT_METHOD = "__init__"s;
}  // namespace

Assignment::Assignment(std::string var, std::unique_ptr<Statement> rv)
    :var_(std::move(var)), rv_(std::move(rv))
{}

ObjectHolder Assignment::Execute(Closure& closure, Context& context)
{
    // Создаем или обновляем значение переменной с именем var_ в closure
    // Используем move, т.к. Execute возвращает созданный внутри себя объект
    closure[var_] = std::move(rv_->Execute(closure, context));
    return closure.at(var_);
}

VariableValue::VariableValue(const std::string& var_name)
//    :var_name_(var_name)
{
    // Переделываем на универсальный алгоритм с использованием вектора
    dotted_ids_.push_back(var_name);
}

VariableValue::VariableValue(std::vector<std::string> dotted_ids)
//    :dotted_ids_(std::move(dotted_ids)), var_name_("")
    :dotted_ids_(std::move(dotted_ids))
{}

ObjectHolder VariableValue::Execute(Closure& closure, [[maybe_unused]] Context& context)
{

    // У нас случай цепочки переменных (объектов x.y.z, они же ClassInstance'ы)
    // Проверим что она не пуста
    if (dotted_ids_.size() > 0)
    {
        runtime::ObjectHolder result;
        
        // Цепочки содержат вложенные closure. Запоминаем указатель на текущую
        Closure* current_closure_ptr = &closure;

        for (const auto& arg : dotted_ids_)
        {
            // Ищем очередной аргумент в текущем словаре closure
            auto arg_it = current_closure_ptr->find(arg);
            if (arg_it == current_closure_ptr->end())
            {
                throw std::runtime_error("Invalid argument name in VariableValue::Execute()"s);
            }

            // Аргумент найден. Проверяем тип соответствующего ему значения в 
            // текущем словаре current_closure, не является ли он сам объектом
            result = arg_it->second;
            auto next_dotted_arg_ptr = result.TryAs<runtime::ClassInstance>();
            // Если тип значения - объект (ClassInstance), то переходим к его
            // closure и будем искать следующий аргумент в нем.
            if (next_dotted_arg_ptr)
            {
                current_closure_ptr = &next_dotted_arg_ptr->Fields();
            }
        }

        // Возвращаем значение для ключа последнего элемента в цепочке
        return result;
    }

    // Во всех других случаях выбрасываем исключение
    throw std::runtime_error("No arguments specified for VariableValue::Execute()"s);

}

unique_ptr<Print> Print::Variable(const std::string& name)
{
    // Вычисляем значение переменной
    //unique_ptr<Statement> name_value_ptr = std::make_unique<VariableValue>(name);
    // Используем конструктор с 1 параметром
    return std::make_unique<Print>(std::make_unique<VariableValue>(name));
    //return std::make_unique<Print>(name_value_ptr);
}

Print::Print(unique_ptr<Statement> argument)
{
    // Из 1 аргумента делаем вектор с 1 элементом
    args_.push_back(std::move(argument));
}

Print::Print(vector<unique_ptr<Statement>> args)
    : args_(std::move(args))
{}

ObjectHolder Print::Execute(Closure& closure, Context& context)
{
    // Обходим весь вектор аргументов и получаем значение каждого
    for (size_t i = 0; i < args_.size(); ++i)
    {
        // Если это не первый элемент вектора,
        // нужно вывести разделяющий пробел
        if (i > 0)
        {
            context.GetOutputStream() << " "s;
        }

        // Вызываем Execute() для текущего аргумента
        runtime::ObjectHolder result = args_[i]->Execute(closure, context);

        // Выводим результат в поток только если он не None (nullptr)
        if (result)
        {
            result->Print(context.GetOutputStream(), context);
        }
        else
        {
            context.GetOutputStream() << "None"s;
        }
    }

    // Добавляем перевод строки в конце вывода
    context.GetOutputStream() << std::endl;
    return runtime::ObjectHolder::None();

}

MethodCall::MethodCall(std::unique_ptr<Statement> object, std::string method,
                       std::vector<std::unique_ptr<Statement>> args)
    :object_(std::move(object)), method_(std::move(method)), args_(std::move(args))
{}

ObjectHolder MethodCall::Execute(Closure& closure, Context& context)
{
    // Если указателя на объект нет, возвращаем None()
    if (!object_)
    {
        return runtime::ObjectHolder::None();
    }

    // Получаем ObjectHolder с ожидаемым объектом через Execute()
    runtime::ObjectHolder callable_object = object_->Execute(closure, context);
    // Проверяем, действительно ли callable_object является объектом
    auto callable_object_ptr = callable_object.TryAs<runtime::ClassInstance>();
    if (callable_object_ptr != nullptr)
    {
        // Вектор значений параметров        
        std::vector<runtime::ObjectHolder> args_values;
        for (const auto& arg : args_)
        {
            // Получаем значения все параметров по заданному в args_ списку аргументов
            args_values.push_back(std::move(arg->Execute(closure, context)));
        }

        // Вызываем метод и получаем результат
        runtime::ObjectHolder result = callable_object_ptr->Call(method_, args_values, context);
        return result;
    }

    // Объект object не был валиден, возвращаем None()
    return runtime::ObjectHolder::None();

}

ObjectHolder Stringify::Execute(Closure& closure, Context& context)
{
    if (!argument_)
    {
        // Аргумент не задан / не существует
        return runtime::ObjectHolder::Own(runtime::String{ "None"s });
    }

    // Получаем ObjectHolder с shared_ptr, указывающим на значение аргумента
    runtime::ObjectHolder exec_result = argument_->Execute(closure, context);
    if (!exec_result)  // Оператор bool(ObjectHolder)
    {
        // Аргумент не найден в closure
        return runtime::ObjectHolder::Own(runtime::String{ "None"s });
    }

    // Вспомогательный контекст, выводящий в строковый поток
    runtime::DummyContext dummy_context;
    // "Печатаем" в поток вспомогательного контекста
    exec_result.Get()->Print(dummy_context.GetOutputStream(), dummy_context);
    return runtime::ObjectHolder::Own(runtime::String{ dummy_context.output.str() });
}

ObjectHolder Add::Execute(Closure& closure, Context& context)
{
    if ((!lhs_) || (!rhs_))
    {
        throw std::runtime_error("No argument(s) specified for Add::Execute()"s);
    }

    // Получаем значения аргументов в ObjectHolder'ы
    runtime::ObjectHolder lhs_exec_result = lhs_->Execute(closure, context);
    runtime::ObjectHolder rhs_exec_result = rhs_->Execute(closure, context);

    {
        // Проверяем случай когда оба аргумента - числа
        auto lhs_value_ptr = lhs_exec_result.TryAs<runtime::Number>();
        auto rhs_value_ptr = rhs_exec_result.TryAs<runtime::Number>();

        if ((lhs_value_ptr != nullptr) && (rhs_value_ptr != nullptr))
        {
            auto lhs_value = lhs_value_ptr->GetValue();
            auto rhs_value = rhs_value_ptr->GetValue();

            return runtime::ObjectHolder::Own(runtime::Number{ lhs_value + rhs_value });
        }

    }

    {
        // Проверяем случай когда оба аргумента - строки
        auto lhs_value_ptr = lhs_exec_result.TryAs<runtime::String>();
        auto rhs_value_ptr = rhs_exec_result.TryAs<runtime::String>();

        if ((lhs_value_ptr != nullptr) && (rhs_value_ptr != nullptr))
        {
            auto& lhs_value = lhs_value_ptr->GetValue();
            auto& rhs_value = rhs_value_ptr->GetValue();

            return runtime::ObjectHolder::Own(runtime::String{ lhs_value + rhs_value });
        }

    }

    {
        // Проверяем что lhs_ это Class и у него есть __add__, принимающий 1 аргумент
        auto lhs_value_ptr = lhs_exec_result.TryAs<runtime::ClassInstance>();

        if (lhs_value_ptr != nullptr)
        {
            const int ARGUMENT_NUM = 1;
            if (lhs_value_ptr->HasMethod(ADD_METHOD, ARGUMENT_NUM))
            {
                return lhs_value_ptr->Call(ADD_METHOD, { rhs_exec_result }, context);
            }
        }
    }

    // Если мы здесь, то ни одно из поддерживаемых сочетаний аргументов не найдено
    throw std::runtime_error("Incompatible argument(s) type(s) for Add::Execute()"s);
}

ObjectHolder Sub::Execute(Closure& closure, Context& context)
{
    if ((!lhs_) || (!rhs_))
    {
        throw std::runtime_error("No argument(s) specified for Sub::Execute()"s);
    }

    // Получаем значения аргументов в ObjectHolder'ы
    runtime::ObjectHolder lhs_exec_result = lhs_->Execute(closure, context);
    runtime::ObjectHolder rhs_exec_result = rhs_->Execute(closure, context);

    {
        // Проверяем случай когда оба аргумента - числа
        auto lhs_value_ptr = lhs_exec_result.TryAs<runtime::Number>();
        auto rhs_value_ptr = rhs_exec_result.TryAs<runtime::Number>();

        if ((lhs_value_ptr != nullptr) && (rhs_value_ptr != nullptr))
        {
            auto lhs_value = lhs_value_ptr->GetValue();
            auto rhs_value = rhs_value_ptr->GetValue();

            return runtime::ObjectHolder::Own(runtime::Number{ lhs_value - rhs_value });
        }

    }

    // Если мы здесь, то ни одно из поддерживаемых сочетаний аргументов не найдено
    throw std::runtime_error("Incompatible argument(s) type(s) for Sub::Execute()"s);
}

ObjectHolder Mult::Execute(Closure& closure, Context& context)
{
    if ((!lhs_) || (!rhs_))
    {
        throw std::runtime_error("No argument(s) specified for Mult::Execute()"s);
    }

    // Получаем значения аргументов в ObjectHolder'ы
    runtime::ObjectHolder lhs_exec_result = lhs_->Execute(closure, context);
    runtime::ObjectHolder rhs_exec_result = rhs_->Execute(closure, context);

    {
        // Проверяем случай когда оба аргумента - числа
        auto lhs_value_ptr = lhs_exec_result.TryAs<runtime::Number>();
        auto rhs_value_ptr = rhs_exec_result.TryAs<runtime::Number>();

        if ((lhs_value_ptr != nullptr) && (rhs_value_ptr != nullptr))
        {
            auto lhs_value = lhs_value_ptr->GetValue();
            auto rhs_value = rhs_value_ptr->GetValue();

            return runtime::ObjectHolder::Own(runtime::Number{ lhs_value * rhs_value });
        }

    }

    // Если мы здесь, то ни одно из поддерживаемых сочетаний аргументов не найдено
    throw std::runtime_error("Incompatible argument(s) type(s) for Mult::Execute()"s);
}

ObjectHolder Div::Execute(Closure& closure, Context& context)
{
    if ((!lhs_) || (!rhs_))
    {
        throw std::runtime_error("No argument(s) specified for Div::Execute()"s);
    }

    // Получаем значения аргументов в ObjectHolder'ы
    runtime::ObjectHolder lhs_exec_result = lhs_->Execute(closure, context);
    runtime::ObjectHolder rhs_exec_result = rhs_->Execute(closure, context);

    {
        // Проверяем случай когда оба аргумента - числа
        auto lhs_value_ptr = lhs_exec_result.TryAs<runtime::Number>();
        auto rhs_value_ptr = rhs_exec_result.TryAs<runtime::Number>();

        if ((lhs_value_ptr != nullptr) && (rhs_value_ptr != nullptr))
        {
            auto lhs_value = lhs_value_ptr->GetValue();
            auto rhs_value = rhs_value_ptr->GetValue();

            if (rhs_value == 0)
            {
                throw std::runtime_error("Division by zero in Div::Execute()"s);
            }

            return runtime::ObjectHolder::Own(runtime::Number{ lhs_value / rhs_value });
        }

    }

    // Если мы здесь, то ни одно из поддерживаемых сочетаний аргументов не найдено
    throw std::runtime_error("Incompatible argument(s) type(s) for Div::Execute()"s);
}


void Compound::AddStatement(std::unique_ptr<Statement> stmt)
{
    statements_.push_back(std::move(stmt));
}


ObjectHolder Compound::Execute(Closure& closure, Context& context)
{
    // По очереди обрабатываем все инструкции
    for (const auto& statement : statements_)
    {
        statement->Execute(closure, context);
    }

    // Возвращаем None() в любом случае
    return runtime::ObjectHolder::None();
}

Return::Return(std::unique_ptr<Statement> statement)
    : statement_(std::move(statement))
{}

ObjectHolder Return::Execute(Closure& closure, Context& context)
{
    // Если указатель на statement_ пуст, бросаем исключение с ObjectHolder::None()
    if (!statement_)
    {
        throw ReturnException(runtime::ObjectHolder::None());
    }

    // В противном случае вычисляем statement_
    runtime::ObjectHolder result = statement_->Execute(closure, context);
    throw ReturnException(result);
}

ClassDefinition::ClassDefinition(ObjectHolder cls)
    : cls_(cls)
{}

ObjectHolder ClassDefinition::Execute(Closure& closure, [[maybe_unused]] Context& context)
{
    // cls_ гарантированно существует, без проверок
    // Получаем указатель на Class
    auto class_ptr = cls_.TryAs<runtime::Class>();
    // Ищем или создаем в closure класс с таким именем и передаем ему наше определение класса
    closure[class_ptr->GetName()] = std::move(cls_);

    // Возвращаем None() в любом случае
    return runtime::ObjectHolder::None();
}

FieldAssignment::FieldAssignment(VariableValue object, std::string field_name,
                                 std::unique_ptr<Statement> rv)
    : object_(std::move(object)), field_name_(std::move(field_name)), rv_(std::move(rv))
{}

ObjectHolder FieldAssignment::Execute(Closure& closure, Context& context)
{
    // Получаем значение для object_
    runtime::ObjectHolder object_value = object_.Execute(closure, context);

    // Если указателя на объект нет, возвращаем None()
    if (!object_value)
    {
        return runtime::ObjectHolder::None();
    }

    // Получаем указатель на объект
    auto object_value_ptr = object_value.TryAs<runtime::ClassInstance>();
    // Создаем или присваиваем полю field_name_ вычисленное значение rv_
    object_value_ptr->Fields()[field_name_] = std::move(rv_->Execute(closure, context));

    return object_value_ptr->Fields().at(field_name_);
}

IfElse::IfElse(std::unique_ptr<Statement> condition, std::unique_ptr<Statement> if_body,
               std::unique_ptr<Statement> else_body)
    : condition_(std::move(condition)), if_body_(std::move(if_body)), else_body_(std::move(else_body))
{}

ObjectHolder IfElse::Execute(Closure& closure, Context& context)
{
    // Если condition_ отсутствует (nullptr), бросаем исключение
    if (!condition_)
    {
        throw std::runtime_error("No condition specified for IfElse::Execute()"s);
    }

    // В противном случае вычисляем condition_
    runtime::ObjectHolder condition_result = condition_->Execute(closure, context);

    if (runtime::IsTrue(condition_result))
    {
        // Условие == True. Выполняем ветку if_body
        return if_body_->Execute(closure, context);
    }
    else if (else_body_)
    {
        // Условие == False. Выполняется если есть else_body
        return else_body_->Execute(closure, context);
    }

    return runtime::ObjectHolder::None();
}

ObjectHolder Or::Execute(Closure& closure, Context& context)
{
    if ((!lhs_) || (!rhs_))
    {
        throw std::runtime_error("Null operands specified for Or::Execute()"s);
    }

    // Получаем значение аргумента в ObjectHolder
    runtime::ObjectHolder lhs_exec_result = lhs_->Execute(closure, context);
    if (runtime::IsTrue(lhs_exec_result))
    {
        return runtime::ObjectHolder::Own(runtime::Bool{ true });
    }

    // Получаем значение аргумента в ObjectHolder
    runtime::ObjectHolder rhs_exec_result = rhs_->Execute(closure, context);
    if (runtime::IsTrue(rhs_exec_result))
    {
        return runtime::ObjectHolder::Own(runtime::Bool{ true });
    }

    // Если мы здесь, оба операнда == false
    return runtime::ObjectHolder::Own(runtime::Bool{ false });
}

ObjectHolder And::Execute(Closure& closure, Context& context)
{
    if ((!lhs_) || (!rhs_))
    {
        throw std::runtime_error("Null operands specified for And::Execute()"s);
    }

    // Получаем значения аргументов в ObjectHolder
    runtime::ObjectHolder lhs_exec_result = lhs_->Execute(closure, context);
    runtime::ObjectHolder rhs_exec_result = rhs_->Execute(closure, context);
    if (runtime::IsTrue(lhs_exec_result) && runtime::IsTrue(rhs_exec_result))
    {
        return runtime::ObjectHolder::Own(runtime::Bool{ true });
    }

    // Если мы здесь, один из операндов == false
    return runtime::ObjectHolder::Own(runtime::Bool{ false });
}

ObjectHolder Not::Execute(Closure& closure, Context& context)
{
    if (!argument_)
    {
        throw std::runtime_error("Null operand specified for Not::Execute()"s);
    }

    // Получаем значение аргумента в ObjectHolder
    runtime::ObjectHolder arg_exec_result = argument_->Execute(closure, context);
    bool result = runtime::IsTrue(arg_exec_result);

    return runtime::ObjectHolder::Own(runtime::Bool{ !result });
}

Comparison::Comparison(Comparator cmp, unique_ptr<Statement> lhs, unique_ptr<Statement> rhs)
    : BinaryOperation(std::move(lhs), std::move(rhs)), cmp_(std::move(cmp))
{}

ObjectHolder Comparison::Execute(Closure& closure, Context& context)
{
    if ((!lhs_) || (!rhs_))
    {
        throw std::runtime_error("Null operands specified for Comparison::Execute()"s);
    }

    // Получаем значения аргументов в ObjectHolder
    runtime::ObjectHolder lhs_exec_result = lhs_->Execute(closure, context);
    runtime::ObjectHolder rhs_exec_result = rhs_->Execute(closure, context);

    // Вызываем функцию-компаратор и передаем ей вычисленные значения аргументов внутри ObjectHolder
    bool result = cmp_(lhs_exec_result, rhs_exec_result, context);

    return runtime::ObjectHolder::Own(runtime::Bool{ result });
}

NewInstance::NewInstance(const runtime::Class& class_, std::vector<std::unique_ptr<Statement>> args)
    : class_instance_(class_), args_(std::move(args))
{}

NewInstance::NewInstance(const runtime::Class& class_)
    : class_instance_(class_)
{}

ObjectHolder NewInstance::Execute(Closure& closure, Context& context)
{
    // Создаем экземпляр класса cls_
    //runtime::ClassInstance class_instance(cls_);  // Перенесено в конструкторы

    // Если у созданного объекта есть метод __init__ с числом параметров,
    // равным числу переданных в векторе аргументов, вызываем его
    if (class_instance_.HasMethod(INIT_METHOD, args_.size()))
    {
        // Вычисляем значения каждого из аргументов вектора аргументов
        std::vector<ObjectHolder> args_values;
        for (const auto& argument : args_)
        {
            args_values.push_back(std::move(argument->Execute(closure, context)));
        }
        // Вызываем метод, передавая ему в качестве параметров вычисленные аргументы
        class_instance_.Call(INIT_METHOD, args_values, context);
    }

    // Share() используем если полем класса будет не Class cls_, 
    // а непосредственно ClassInstance class_instance_
    return runtime::ObjectHolder::Share(class_instance_);
    // Мы создаем Instance непосредственно в теле Execute(), поэтому
    // передаем его через Own() + move
    //return runtime::ObjectHolder::Own(std::move(class_instance_));
}

MethodBody::MethodBody(std::unique_ptr<Statement>&& body)
    :body_(std::move(body))
{}

ObjectHolder MethodBody::Execute(Closure& closure, Context& context)
{
    // Возвращаем None если указатель на body_ равен nullptr
    if (!body_)
    {
        return runtime::ObjectHolder::None();
    }

    // Мы должны перехватить исключение, генерируемое инструкцией Return
    // (если такая встретилась внутри body_)
    try
    {
        body_->Execute(closure, context);
    }
    catch (ReturnException& rex)  // Объекты исключений принимаются по ссылке
    {
        // Исключение поймано. Возвращаем результат инструкции Return
        return rex.GetValue();
    }

    // В противном случае ничего не возвращаем
    return runtime::ObjectHolder::None();
}

ReturnException::ReturnException(const runtime::ObjectHolder& rex_value)
    : rex_value_(rex_value)
{}

runtime::ObjectHolder ReturnException::GetValue()
{
    return rex_value_;
}

}  // namespace ast
