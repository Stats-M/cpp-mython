/*
 Класс лексического анализатора для разбора программы на языке Mini-python (Mython). 
 Чтение данных производится из входящего потока std::istream
*/

#pragma once

#include <iosfwd>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <map>
#include <cctype>    // for isalpha() etc.

namespace parse
{

// Пространство имен всех доступных типов токенов (лексем языка)
namespace token_type
{
struct Number
{
    // Lexeme «Number»
    int value;
};

struct Id
{
    // Lexeme «ID»
    ::std::string value;
};

struct Char
{
    // Lexeme «Char»
    char value;
};

struct String
{  
    // Lexem «const String»
    ::std::string value;
};

struct Class
{
    // Лексема «class»
};
struct Return
{
    // Лексема «return»
};
struct If
{
    // Лексема «if»
};
struct Else
{
    // Лексема «else»
};
struct Def
{
    // Лексема «def»
};
struct Newline
{
    // Лексема «конец строки»
};
struct Print
{
    // Лексема «print»
};
struct Indent
{
    // Лексема «увеличение отступа», соответствует двум пробелам
};
struct Dedent
{
    // Лексема «уменьшение отступа»
};
struct Eof
{
    // Лексема «конец файла»
};
struct And
{
    // Лексема «and»
};
struct Or
{
    // Лексема «or»
};
struct Not
{
    // Лексема «not»
};
struct Eq
{
    // Лексема «==»
};
struct NotEq
{
    // Лексема «!=»
};
struct LessOrEq
{
    // Лексема «<=»
};
struct GreaterOrEq
{
    // Лексема «>=»
};
struct None
{
    // Лексема «None»
};
struct True
{
    // Лексема «True»
};
struct False
{
    // Лексема «False»
};

}  // namespace token_type

using TokenBase
= std::variant<token_type::Number, token_type::Id, token_type::Char, token_type::String,
    token_type::Class, token_type::Return, token_type::If, token_type::Else,
    token_type::Def, token_type::Newline, token_type::Print, token_type::Indent,
    token_type::Dedent, token_type::And, token_type::Or, token_type::Not,
    token_type::Eq, token_type::NotEq, token_type::LessOrEq, token_type::GreaterOrEq,
    token_type::None, token_type::True, token_type::False, token_type::Eof>;

// Структура Token - основная единица информации лексера
struct Token : TokenBase
{
    // Makes available all std::variant constructors
    using TokenBase::TokenBase;

    template <typename T>
    [[nodiscard]] bool Is() const
    {
        return std::holds_alternative<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T& As() const
    {
        return std::get<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T* TryAs() const
    {
        return std::get_if<T>(this);
    }
};

bool operator==(const Token& lhs, const Token& rhs);
bool operator!=(const Token& lhs, const Token& rhs);

std::ostream& operator<<(std::ostream& os, const Token& rhs);

class LexerError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};


class Lexer
{
public:
    // Конструктор для указанного потока ввода
    explicit Lexer(std::istream& input);

    // Возвращает ссылку на текущий токен или token_type::Eof, если поток токенов закончился
    [[nodiscard]] const Token& CurrentToken() const;

    // Возвращает следующий токен, либо token_type::Eof, если поток токенов закончился
    Token NextToken();

    // Если текущий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    // USAGE SAMPLE:
    // lexer.Expect<token_type::Class>(); - checks that current token is Class
    template <typename T>
    const T& Expect() const
    {
        using namespace std::literals;
        if (!(*current_token_it_).Is<T>())
        {
            throw LexerError("Token::Expect() method has failed."s);
        }
        return CurrentToken().As<T>();
    }

    // Метод проверяет, что текущий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    // USAGE SAMPLE:
    // lexer.Expect<token_type::Char>(':'); - checks that current token is Char with valie ':'
    template <typename T, typename U>
    void Expect(const U& value) const
    {
        using namespace std::literals;
        // Создаем на основе value другой токен типа T...
        Token other_token(T{ value });
        // ... и сравниваем его значение со значением текущего токена
        if (*current_token_it_ != other_token)
        {
            throw LexerError("Token::Expect(value) method has failed."s);
        }
        // Ошибки нет, выход
    }

    // Если следующий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    // USAGE SAMPLE:
    // auto name = lexer.ExpectNext<token_type::Id>().value;
    template <typename T>
    const T& ExpectNext()
    {
        NextToken();
        return Expect<T>();
    }

    // Метод проверяет, что следующий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    // USAGE SAMPLE:
    // lexer.ExpectNext<token_type::Char>(':'); - checks that next token is Char with valie ':'
    template <typename T, typename U>
    void ExpectNext(const U& value)
    {
        NextToken();
        Expect<T>(value);
    }

private:
    // Количество пробелов на 1 отступ
    const int SPACES_PER_INDENT = 2;

    // Словарь ключевых слов программы на языке Mython,
    // которым соответствуют определенные лексемы
    const std::map<std::string, Token> keywords_map_ =
    {
        {std::string{"class"},  token_type::Class{} },
        {std::string{"return"}, token_type::Return{}},
        {std::string{"if"},     token_type::If{}    },
        {std::string{"else"},   token_type::Else{}  },
        {std::string{"def"},    token_type::Def{}   },
        {std::string{"print"},  token_type::Print{} },
        {std::string{"and"},    token_type::And{}   },
        {std::string{"or"},     token_type::Or{}    },
        {std::string{"not"},    token_type::Not{}   },
        {std::string{"None"},   token_type::None{}  },
        {std::string{"True"},   token_type::True{}  },
        {std::string{"False"},  token_type::False{} }
    };

    // Вектор лексем разобранного текста программы (результат работы лексера)
    std::vector<Token> tokens_;
    // Итератор, указывающий на текущий токен
    std::vector<Token>::const_iterator current_token_it_;
    // Const ссылка на поток ввода лексера
    const std::istream& in_stream_;
    // Глобальный счетчик отступов. Возможно, перенести в ParseInputStream() или сделать static
    int global_indent_counter_ = 0;

    // Точка входа разбора текста программы на лексемы
    void ParseInputStream(std::istream& istr);
    //Альтенативная форма с возвратом итератора
    //std::vector<Token>::const_iterator ParseInputStreamEx(std::istream& istr);

    // Обработка отступов
    void ParseIndent(std::istream& istr);
    // Обработка строк и экранированных символов
    void ParseString(std::istream& istr);
    // Обработка ключевых слов и идентификаторов
    void ParseKeywords(std::istream& istr);
    // Обработка символов
    void ParseChars(std::istream& istr);
    // Обработка чисел
    void ParseNumbers(std::istream& istr);
    // Обработка потока на наличие конца строки
    void ParseNewLine(std::istream& istr);
    // Обработка комментариев
    void ParseComments(std::istream& istr);

    // Обрезаем лидирующие пробелы
    void TrimSpaces(std::istream& istr);
};

}  // namespace parse
