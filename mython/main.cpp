#include "lexer.h"
#include "parse.h"
#include "runtime.h"
#include "statement.h"
#include "test_runner_p.h"

#include <iostream>

using namespace std;

namespace parse
{
void RunOpenLexerTests(TestRunner& tr);
}  // namespace parse

namespace ast
{
void RunUnitTests(TestRunner& tr);
}
namespace runtime
{
void RunObjectHolderTests(TestRunner& tr);
void RunObjectsTests(TestRunner& tr);
}  // namespace runtime

void TestParseProgram(TestRunner& tr);

namespace
{

void RunMythonProgram(istream& input, ostream& output)
{
    parse::Lexer lexer(input);
    auto program = ParseProgram(lexer);

    runtime::SimpleContext context{ output };
    runtime::Closure closure;
    program->Execute(closure, context);
}

void TestSimplePrints()
{
    istringstream input(R"(
print 57
print 10, 24, -8
print 'hello'
print "world"
print True, False
print
print None
)");

    ostringstream output;
    RunMythonProgram(input, output);

    ASSERT_EQUAL(output.str(), "57\n10 24 -8\nhello\nworld\nTrue False\n\nNone\n");
}

void TestAssignments()
{
    istringstream input(R"(
x = 57
print x
x = 'C++ black belt'
print x
y = False
x = y
print x
x = None
print x, y
)");

    ostringstream output;
    RunMythonProgram(input, output);

    ASSERT_EQUAL(output.str(), "57\nC++ black belt\nFalse\nNone False\n");
}

void TestArithmetics()
{
    istringstream input("print 1+2+3+4+5, 1*2*3*4*5, 1-2-3-4-5, 36/4/3, 2*5+10/2");

    ostringstream output;
    RunMythonProgram(input, output);

    ASSERT_EQUAL(output.str(), "15 120 -13 3 15\n");
}

void TestVariablesArePointers()
{
    istringstream input(R"(
class Counter:
  def __init__():
    self.value = 0

  def add():
    self.value = self.value + 1

class Dummy:
  def do_add(counter):
    counter.add()

x = Counter()
y = x

x.add()
y.add()

print x.value

d = Dummy()
d.do_add(x)

print y.value
)");

    ostringstream output;
    RunMythonProgram(input, output);

    ASSERT_EQUAL(output.str(), "2\n3\n");
}



void MyTest_SelfAssignment()
{
    istringstream input(R"(
class Cls1:
  def __init__():
    self.x = 456

  def __str__():
    return str(self.x)

class Cls2:
  def __init__():
    self.value = 123
    self.str_ = 'Abc'
    self.boo = True
    self.boo_f = False
    self.none = None
    self.cls_ = Cls1()

  def __str__() :
    return str(self.cls_) + ' ' + str(self.none) + ' ' + str(self.value) + ' ' + str(self.str_) + ' ' + str(self.boo) + ' ' + str(self.boo_f)

x = Cls2()

print x
)");

    ostringstream output;
    RunMythonProgram(input, output);
    ASSERT_EQUAL(output.str(), "456 None 123 Abc True False\n");
}



void MyTest_SelfReassignment()
{
    istringstream input(R"(
class OtherCLS:
  def __init__():
    self.x = "OtherCLS"

  def __str__():
    return str(self.x)

class Cls:
  def __init__():
    self.value_ = 0

  def SetValue(value):
    self.value_ = value

  def __str__() :
    return str(self.value_)

x = Cls()
print x

y = 234
x.SetValue(y)
print x

y = OtherCLS()
x.SetValue(y)
print x

y = "Str"
x.SetValue("Str")
print x

x.SetValue(None)
print x

x.SetValue(True)
print x

x.SetValue(False)
print x

)");

    ostringstream output;
    RunMythonProgram(input, output);
    ASSERT_EQUAL(output.str(), "0\n234\nOtherCLS\nStr\nNone\nTrue\nFalse\n");
}



void MyTest_ShortSelf()
{
    istringstream input(R"(
class X:
  def __init__(p):
    p.x = self

class XHolder:
  def __init__():
    dummy = 0

xh = XHolder()
x = X(xh)
)");

    ostringstream output;
    RunMythonProgram(input, output);
}


void TestAll()
{
    TestRunner tr;
    parse::RunOpenLexerTests(tr);
    runtime::RunObjectHolderTests(tr);
    runtime::RunObjectsTests(tr);
    ast::RunUnitTests(tr);
    TestParseProgram(tr);

    RUN_TEST(tr, TestSimplePrints);
    RUN_TEST(tr, TestAssignments);
    RUN_TEST(tr, TestArithmetics);
    RUN_TEST(tr, TestVariablesArePointers);

    // Дополнительные тесты для gcc
    RUN_TEST(tr, MyTest_SelfAssignment);
    RUN_TEST(tr, MyTest_SelfReassignment);
    RUN_TEST(tr, MyTest_ShortSelf);
}

}  // namespace

int main()
{
    try
    {
        TestAll();

        RunMythonProgram(cin, cout);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}
