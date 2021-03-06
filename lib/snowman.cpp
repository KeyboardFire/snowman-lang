#include "snowman.hpp"
#include "retrieval.hpp"
#include <iostream>   // std::cout, std::cerr, std::endl
#include <chrono>     // time stuffs
#include <cstdlib>    // rand, srand
#include <cmath>      // abs, fmod, pow, ceil, floor, round
#include <algorithm>  // find
#include <regex>      // obvious
// included from snowman.hpp: <vector>, <string>, <map>, <stdexcept>

#define DEBUG

#define HSH1(a) ((long)a)
#define HSH2(a,b) (((long)a)*256 + ((long)b))
#define HSH3(a,b,c) (((long)a)*256*256 + ((long)b)*256 + ((long)c))

#define ROT2(a,b) v = vars[a]; vars[a] = vars[b]; vars[b] = v;
#define ROT3(a,b,c) v = vars[a]; vars[a] = vars[b]; vars[b] = vars[c]; vars[c] = v;

#define TOG_ACT(n) activeVars[n] = !activeVars[n]
#define ROT_ACT() b = activeVars[0]; activeVars[0] = activeVars[3]; \
    activeVars[3] = activeVars[5]; activeVars[5] = activeVars[6]; \
    activeVars[6] = activeVars[7]; activeVars[7] = activeVars[4]; \
    activeVars[4] = activeVars[2]; activeVars[2] = activeVars[1]; \
    activeVars[1] = b;

const std::string DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz";
const int TOBASE_PRECISION = 10; // number of digits after decimal point
const double TOBASE_EPSILON = 0.00001; // if the decimal part is less than
                                       // this, it will be treated as an int

// constructor/destructor
Snowman::Snowman(): activeVars{false}, activePermavar(0),
        savedActiveState{false}, debugOutput(false) {
    srand(std::chrono::duration_cast<std::chrono::milliseconds>
        (std::chrono::system_clock::now().time_since_epoch()).count());
}
Snowman::~Snowman() {}

// execute string of code
void Snowman::run(std::string code) {
    std::vector<std::string> tokens;
    try {
        tokens = Snowman::tokenize(code);
    } catch (SnowmanException& se) {
        std::cerr << "SnowmanException thrown at tokenize" << std::endl;
        std::cerr << "  what():  " << se.what() << std::endl;
        // all exceptions are fatal because then we have no tokens to run
        std::cerr << "fatal error, aborting" << std::endl;
        return;
    }
    for (std::string s : tokens) {
        try {
            evalToken(s);
        } catch (SnowmanException& se) {
            std::cerr << "SnowmanException thrown at evalToken" << std::endl;
            std::cerr << "  what():  " << se.what() << std::endl;
            if (se.fatal) {
                std::cerr << "fatal error, aborting" << std::endl;
                return;
            } else {
                std::cerr << "non-fatal error, continuing" << std::endl;
                continue;
            }
        }
        if (debugOutput) {
            std::cout << "<[T]> " << s << std::endl;
            std::cout << "<[D]> " << debug();
        }
    }
}

// static method to convert string of code into tokens (individual
// instructions)
std::vector<std::string> Snowman::tokenize(std::string code) {
    std::vector<std::string> tokens;
    std::string token;
    bool comment = false, blockComment = false, prevCloseBracket = false,
         escaping = false;
    for (char& c : code) {
        if (comment) {
            if (c == '\n') comment = false;
            else continue;
        }

        if (blockComment) {
            if (c == ']') {
                if (prevCloseBracket) blockComment = prevCloseBracket = false;
                else prevCloseBracket = true;
            } else {
                prevCloseBracket = false;
            }
            continue;
        }

        if ((token[0] != '"' && token[0] != ':') && !(c >= '!' && c <= '~')) {
            // ignore non-printable-ASCII outside of string/block literals
            continue;
        }

        if (token[0] >= '0' && token[0] <= '9' && !(c >= '0' && c <= '9')) {
            tokens.push_back(token);
            token = "";
        }

        if (token[0] >= '0' && token[0] <= '9') {
            // c is guaranteed to be a digit
            token += c;
        } else if (token[0] >= 'a' && token[0] <= 'z') {
            // two-letter operator in progress
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                token += c;
                tokens.push_back(token);
                token = "";
            } else {
                throw SnowmanException("at tokenize: letter operator "
                    "terminated prematurely?", true);
            }
        } else if (token[0] >= 'A' && token[0] <= 'Z') {
            // three-letter operator in progress
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                token += c;
                if (token.length() == 3) {
                    tokens.push_back(token);
                    token = "";
                }
            } else {
                throw SnowmanException("at tokenize: letter operator "
                    "terminated prematurely?", true);
            }
        } else if (token[0] == '"') {
            // string literal in progress
            token += c;
            if (c == '"') {
                if (escaping) {
                    token.erase(token.length() - 2, 1); // get rid of backslash
                    escaping = false;
                } else {
                    tokens.push_back(token);
                    token = "";
                }
            } else if (c == '\\') {
                if (escaping) {
                    token.erase(token.length() - 2, 1); // get rid of backslash
                    escaping = false;
                } else {
                    escaping = true;
                }
            } else if (escaping) escaping = false;
        } else if (token[0] == '=') {
            // permavar switch in progress
            token += c;
            if ((c == '+') || (c == '!')) {
                tokens.push_back(token);
                token = "";
            } else if (c != '=') {
                throw SnowmanException("at tokenize: invalid permavar name?",
                    true);
            }
        } else if (token.length() == 0) {
            // nothing currently in progress; start new token
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') || (c == '"') || (c == '=')) {
                // some token that is longer than one character
                token += c;
                // allow token to continue to be added to
            } else /*if (c >= '!' && c <= '~')*/ {
                // single character token
                // (printable ascii is already guaranteed from if-continue
                //  above)
                if ((c == '/') && (tokens.size() > 0) &&
                        (tokens[tokens.size()-1] == "/")) {
                    // comment
                    tokens.pop_back();
                    comment = true;
                } else if ((c == '[') && (tokens.size() > 0) &&
                        (tokens[tokens.size()-1] == "[")) {
                    // block comment
                    tokens.pop_back();
                    blockComment = true;
                } else if ((c == '(') && (tokens.size() > 0) &&
                        (tokens[tokens.size()-1] == "(")) {
                    // subroutine start
                    tokens.pop_back();
                    tokens.push_back("((");
                } else if ((c == ')') && (tokens.size() > 0) &&
                        (tokens[tokens.size()-1] == ")")) {
                    // subroutine end
                    tokens.pop_back();
                    tokens.push_back("))");
                } else if (c == ';') {
                    // end block literal
                    std::string blk = ";", t;
                    while ((t = tokens.back()) != ":") {
                        blk = t + blk;
                        tokens.pop_back();
                        if (tokens.size() == 0) {
                            throw SnowmanException("at tokenize: invalid "
                                "block nesting?", true);
                        }
                    }
                    tokens[tokens.size()-1] += blk;
                } else {
                    token += c;
                    tokens.push_back(token);
                    token = "";
                }
            }
        } else {
            throw SnowmanException("at tokenize: token started with unknown"
                "value?", true);
        }
    }
    if (token.length() != 0) tokens.push_back(token);
    return tokens;
}

// execute an individual token (called in a loop over all tokens)
void Snowman::evalToken(std::string token) {
    // used for letter operators, 2nd and 3rd if blocks below
    // (initialized to false because compiler warnings)
    bool consume = false;
    if (token[0] >= '0' && token[0] <= '9') {
        // store literal number
        int num;
        try {
            num = std::stoi(token);
        } catch (const std::invalid_argument& e) {
            store(Variable(0.0));
            throw SnowmanException("at evalToken: invalid number " + token +
                "? using 0 instead", false);
        } catch (const std::out_of_range& e) {
            store(Variable(0.0));
            throw SnowmanException("at evalToken: number " + token + " out of "
                "range, using 0 instead", false);
        }
        store(Variable((tNum)num));
        return;
    } else if (token.length() == 2 && token[0] >= 'a' && token[0] <= 'z') {
        // two-letter operator
        if (token[1] >= 'A' && token[1] <= 'Z') {
            consume = true;
            // convert to lowercase
            token[1] = token[1] + ('a' - 'A');
        } else {
            consume = false;
        }
        // handled further below
    } else if (token.length() == 3 && token[0] >= 'A' && token[0] <= 'Z') {
        // three-letter operator
        if ((token[1] >= 'a' && token[1] <= 'z') &&
                (token[2] >= 'A' && token[2] <= 'Z')) {
            consume = true;
            // convert to all uppercase
            token[1] = token[1] - ('a' - 'A');
        } else if ((token[1] >= 'A' && token[1] <= 'Z') &&
                (token[2] >= 'a' && token[2] <= 'z')) {
            consume = false;
            // convert to all uppercase
            token[2] = token[2] - ('a' - 'A');
        } else {
            throw SnowmanException("at evalToken: bad letter function "
                "capitalization, ignoring token", false);
        }
        // handled further below
    } else if (token.length() >= 2 && token[0] == '"') {
        // store literal string-array
        auto arr = new tArray(token.length() - 2);
        for (vvs i = 1; i < token.length() - 1; ++i) {
            (*arr)[i-1] = Variable((tNum)token[i]);
        }
        store(Variable(arr));
        return;
    } else if (token.length() >= 2 && token[0] == ':') {
        // store literal block
        auto str = new std::string(token.substr(1, token.length() - 2));
        store(Variable(str));
        return;
    } else if ((token[0] == '=') || (token[0] == '+') || (token[0] == '!')) {
        // switch permavar
        activePermavar = (token.length()-1) * 2 +
            (token[token.length()-1] == '!');
        return;
    } else if (token == "((") {
        VarState vs;
        std::memcpy(vs.vars, vars, sizeof(Variable)*8);
        std::memcpy(vs.activeVars, activeVars, sizeof(bool)*8);
        std::fill(std::begin(vars), std::end(vars), Variable());
        std::fill(std::begin(activeVars), std::end(activeVars), false);
        subroutines.push_back(vs);
        return;
    } else if (token == "))") {
        if (subroutines.size() == 0) {
            throw SnowmanException("at evalToken: no subroutines left on "
                "stack, ignoring `))' instruction", false);
        }
        VarState vs = subroutines.back();
        std::memcpy(vars, vs.vars, sizeof(Variable)*8);
        std::memcpy(activeVars, vs.activeVars, sizeof(bool)*8);
        subroutines.pop_back();
        return;
    } else if (token.length() == 1 && token[0] >= '!' && token[0] <= '~') {
        // handled below
    } else {
        throw SnowmanException("at evalToken: unrecognized token?", true);
    }

    // compute a hash for each token, so that we can use a switch statement
    // below. see also #define'd HSH1, HSH2, and HSH3
    long token_hsh = 0;
    for (char& ch : token) {
        token_hsh *= 256;
        token_hsh += ch;
    }

    // and now, time for...
    // THE HUGE SWITCH STATEMENT! (this contains all operators, letter or
    //   otherwise)

    Variable v; // for variable operators (ROT2, ROT3)
    bool b; // for active variable rotation operators (ROT_ACT)

    switch (token_hsh) {

    /// Rotation operators
    case HSH1('/'): /// cf
        ROT2(2, 5); break;
    case HSH1('\\'): /// ah
        ROT2(0, 7); break;
    case HSH1('_'): /// fh
        ROT2(5, 7); break;
    case HSH1('['): /// af
        ROT2(0, 5); break;
    case HSH1(']'): /// ch
        ROT2(2, 7); break;
    case HSH1('|'): /// bg
        ROT2(1, 6); break;
    case HSH1('-'): /// de
        ROT2(3, 4); break;
    case HSH1('\''): /// bd
        ROT2(1, 3); break;
    case HSH1('`'): /// be
        ROT2(1, 4); break;
    case HSH1(','): /// eg
        ROT2(4, 6); break;
    case HSH1('.'): /// dg
        ROT2(3, 6); break;
    case HSH1('^'): /// dbe
        ROT3(1, 3, 4); break;
    case HSH1('>'): /// aef
        ROT3(5, 4, 0); break;
    case HSH1('<'): /// cdh
        ROT3(2, 3, 7); break;

    /// Active variable operators
    case HSH1('('): /// af
        TOG_ACT(0); TOG_ACT(5); break;
    case HSH1(')'): /// ch
        TOG_ACT(2); TOG_ACT(7); break;
    case HSH1('{'): /// bdg
        TOG_ACT(1); TOG_ACT(3); TOG_ACT(6); break;
    case HSH1('}'): /// beg
        TOG_ACT(1); TOG_ACT(4); TOG_ACT(6); break;
    case HSH1('~'): /// invert all (abcdefgh)
        TOG_ACT(0); TOG_ACT(1); TOG_ACT(2); TOG_ACT(3); TOG_ACT(4); TOG_ACT(5);
        TOG_ACT(6); TOG_ACT(7); break;
    case HSH1('@'): /// rotate (done clockwise, abcehgfd -> bcehgfda)
        ROT_ACT(); break;
    case HSH1('%'): /// reflect (abcehgfd -> hgfdabce)
        ROT_ACT(); ROT_ACT(); ROT_ACT(); ROT_ACT(); break;
    case HSH1('?'): /// mark all as inactive
        activeVars[0] = activeVars[1] = activeVars[2] = activeVars[3] =
            activeVars[4] = activeVars[5] = activeVars[6] = activeVars[7] =
            false; break;
    case HSH1('$'): /// save current state
        // woo, C-like memory management
        std::memcpy(savedActiveState, activeVars, sizeof(bool)*8);
        break;
    case HSH1('&'): /// restore saved state
        std::memcpy(activeVars, savedActiveState, sizeof(bool)*8);
        break;

    /// Permavar operators
    case HSH1('*'): /// retrieve a value, set the current permavar's value to this
        permavars[activePermavar] = retrieve(-1, true, -1);
        break;
    case HSH1('#'): /// store the current permavar's value
        store(permavars[activePermavar].copy());
        break;

    /// Number operators
    case HSH3('N','D','E'): { /// (n) -> n: decrement
        Retrieval<tNum> r(this, consume);
        store(Variable(r.a - 1));
        break;
    }
    case HSH3('N','I','N'): { /// (n) -> n: increment
        Retrieval<tNum> r(this, consume);
        store(Variable(r.a + 1));
        break;
    }
    case HSH3('N','A','B'): { /// (n) -> n: absolute value
        Retrieval<tNum> r(this, consume);
        store(Variable(r.a < 0 ? -r.a : r.a));
        break;
    }
    case HSH2('n','f'): { /// (n) -> n: floor
        Retrieval<tNum> r(this, consume);
        store(Variable(floor(r.a)));
        break;
    }
    case HSH2('n','c'): { /// (n) -> n: ceiling
        Retrieval<tNum> r(this, consume);
        store(Variable(ceil(r.a)));
        break;
    }
    case HSH3('N','R','O'): { /// (n) -> n: round
        Retrieval<tNum> r(this, consume);
        store(Variable(round(r.a)));
        break;
    }
    case HSH3('N','B','N'): { /// (n) -> n: bitwise NOT
        Retrieval<tNum> r(this, consume);
        store(Variable((tNum) ~((int)round(r.a))));
        break;
    }
    case HSH3('N','B','O'): { /// (nn) -> n: bitwise OR
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable((tNum) (((int)round(r.a)) | ((int)round(r.b)))));
        break;
    }
    case HSH3('N','B','A'): { /// (nn) -> n: bitwise AND
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable((tNum) (((int)round(r.a)) & ((int)round(r.b)))));
        break;
    }
    case HSH3('N','B','X'): { /// (nn) -> n: bitwise XOR
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable((tNum) (((int)round(r.a)) ^ ((int)round(r.b)))));
        break;
    }
    case HSH2('n','a'): { /// (nn) -> n: addition
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable(r.a + r.b));
        break;
    }
    case HSH2('n','s'): { /// (nn) -> n: subtraction
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable(r.a - r.b));
        break;
    }
    case HSH2('n','m'): { /// (nn) -> n: multiplication
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable(r.a * r.b));
        break;
    }
    case HSH2('n','d'): { /// (nn) -> n: division
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable(r.a / r.b));
        break;
    }
    case HSH3('N','M','O'): { /// (nn) -> n: modulo
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable(fmod(r.a, r.b)));
        break;
    }
    case HSH2('n','l'): { /// (nn) -> n: less than
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable((tNum)(r.a < r.b)));
        break;
    }
    case HSH2('n','g'): { /// (nn) -> n: greater than
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable((tNum)(r.a > r.b)));
        break;
    }
    case HSH2('n','r'): { /// (nn) -> n: range
        Retrieval<tNum, tNum> r(this, consume);
        auto arr = new tArray;
        bool rev = (r.a > r.b);
        for (tNum i = r.a; rev ? (i > r.b) : (i < r.b); i += (rev ? -1 : 1)) {
            arr->push_back(Variable(i));
        }
        store(Variable(arr));
        break;
    }
    case HSH2('n','p'): { /// (nn) -> n: power
        Retrieval<tNum, tNum> r(this, consume);
        store(Variable(pow(r.a, r.b)));
        break;
    }
    case HSH2('n','b'): { /// (nn) -> a: to base
        Retrieval<tNum, tNum> r(this, consume);
        // convert integer part
        int n = floor(r.a), base = round(r.b);
        if (base <= 0) {
            throw SnowmanException("at nb: negative or 0 base, stopping "
                "execution of nb", false);
        }
        bool neg = n < 0;
        if (neg) n = -n;
        std::string nb;
        while (n) {
            nb = DIGITS[n % base] + nb;
            n /= base;
        }
        if (neg) nb = '-' + nb;
        if (nb.empty()) nb = "0";
        // convert decimal part
        tNum decimalPart = r.a - floor(r.a);
        if (decimalPart > TOBASE_EPSILON) {
            nb += '.';
            for (int count = 0; (count < TOBASE_PRECISION) &&
                    (decimalPart > TOBASE_EPSILON); ++count) {
                decimalPart *= base;
                int digit = floor(decimalPart);
                nb += DIGITS[digit];
                decimalPart -= digit;
            }
        }
        store(stringToArr(nb));
        break;
    }

    /// Array operators
    case HSH3('A','S','O'): { /// (a) -> a: sort
        Retrieval<tArray*> r(this, consume);
        std::sort(r.a->begin(), r.a->end());
        store(Variable(new tArray(*r.a)));
        break;
    }
    case HSH3('A','S','B'): { /// (ab) -> a: sort by
        Retrieval<tArray*, std::string*> r(this, consume);
        std::sort(r.a->begin(), r.a->end(),
            [&] (Variable const& a, Variable const& b) {
                store(a);
                store(b);
                run(*r.b);
                return Retrieval<bool>(this).b;
            });
        store(Variable(new tArray(*r.a)));
        break;
    }
    case HSH2('a','f'): { /// (ab) -> *: fold
        Retrieval<tArray*, std::string*> r(this, consume);
        if (r.a->size() == 0) {
            store(Variable(0.0));  // this is just arbitrary
        } else {
            store((*r.a)[0]);
            for (vvs i = 1; i < r.a->size(); ++i) {
                store((*r.a)[i]);
                run(*r.b);
            }
        }
        break;
    }
    case HSH2('a','c'): { /// (aa) -> a: concatenate arrays
        Retrieval<tArray*, tArray*> r(this, consume);
        int s1 = r.a->size(), s2 = r.b->size();
        auto arr = new tArray(s1 + s2);
        for (int i = 0; i < s1; ++i) (*arr)[i] = (*r.a)[i];
        for (int i = 0; i < s2; ++i) (*arr)[s1+i] = (*r.b)[i];
        store(Variable(arr));
        break;
    }
    case HSH2('a','d'): { /// (aa) -> a: array/set difference
        Retrieval<tArray*, tArray*> r(this, consume);
        auto arr = new tArray;
        for (vvs i = 0; i < r.a->size(); ++i) {
            if (std::find(r.b->begin(), r.b->end(), (*r.a)[i]) == r.b->end()) {
                arr->push_back((*r.a)[i]);
            }
        }
        store(Variable(arr));
        break;
    }
    case HSH3('A','O','R'): { /// (aa) -> a: setwise or
        Retrieval<tArray*, tArray*> r(this, consume);
        auto arr = new tArray;
        for (Variable v : *r.a) {
            if (std::find(arr->begin(), arr->end(), v) == arr->end()) {
                arr->push_back(v);
            }
        }
        for (Variable v : *r.b) {
            if (std::find(arr->begin(), arr->end(), v) == arr->end()) {
                arr->push_back(v);
            }
        }
        store(Variable(arr));
        break;
    }
    case HSH3('A','A','N'): { /// (aa) -> a: setwise and
        Retrieval<tArray*, tArray*> r(this, consume);
        auto arr = new tArray;
        for (vvs i = 0; i < r.a->size(); ++i) {
            if (std::find(r.b->begin(), r.b->end(), (*r.a)[i]) != r.b->end() &&
                    std::find(arr->begin(), arr->end(), (*r.a)[i]) ==
                    arr->end()) {
                arr->push_back((*r.a)[i]);
            }
        }
        store(Variable(arr));
        break;
    }
    case HSH2('a','r'): { /// (an) -> a: array repeat
        Retrieval<tArray*, tNum> r(this, consume);
        if (r.b < 0) r.b = 0;
        auto arr = new tArray(r.a->size() * r.b);
        for (vvs i = 0; i < r.a->size() * r.b; ++i) {
            (*arr)[i] = (*r.a)[i % r.a->size()];
        }
        store(Variable(arr));
        break;
    }
    case HSH2('a','j'): { /// (aa) -> a: array join
        Retrieval<tArray*, tArray*> r(this, consume);
        if (r.a->size() < 2) {
            store(new tArray(*r.a));
        } else {
            auto arr = new tArray;
            for (auto it = r.a->begin(); it != std::prev(r.a->end()); ++it) {
                arr->push_back(*it);
                arr->insert(arr->end(), r.b->begin(), r.b->end());
            }
            arr->push_back((*r.a)[r.a->size() - 1]);
            store(Variable(arr));
        }
        break;
    }
    case HSH2('a','s'): { /// (aa) -> a: split
        Retrieval<tArray*, tArray*> r(this, consume);
        auto arr = new tArray, tmp = new tArray;
        for (vvs i = 0; i < r.a->size(); ++i) {
            if ((*r.b) == tArray(r.a->begin() + i,
                    r.a->begin() + i + r.b->size())) {
                arr->push_back(Variable(tmp));
                tmp = new tArray;
                i += r.b->size() - 1;
            } else {
                tmp->push_back((*r.a)[i]);
            }
        }
        arr->push_back(Variable(tmp));
        store(Variable(arr));
        break;
    }
    case HSH2('a','g'): { /// (an) -> a: split array in groups of size
        Retrieval<tArray*, tNum> r(this, consume);
        vvs n = round(r.b);
        if (n <= 0) {
            throw SnowmanException("at ag: negative or 0 n, stopping "
                "execution of az", false);
        }
        auto arr = new tArray, tmp = new tArray;
        for (vvs i = 0; i < r.a->size(); ++i) {
            tmp->push_back((*r.a)[i]);
            if (i % n == (n-1)) {
                arr->push_back(Variable(tmp));
                tmp = new tArray;
            }
        }
        if (tmp->size()) arr->push_back(Variable(tmp));
        else delete tmp;
        store(Variable(arr));
        break;
    }
    case HSH2('a','e'): { /// (ab) -> -: each
        Retrieval<tArray*, std::string*> r(this, consume);
        for (Variable v : *r.a) {
            store(v);
            run(*r.b);
        }
        break;
    }
    case HSH2('a','m'): { /// (ab) -> a: map
        Retrieval<tArray*, std::string*> r(this, consume);
        auto arr = new tArray;
        for (Variable v : *r.a) {
            store(v);
            run(*r.b);
            Retrieval<Variable> r2(this, true);
            arr->push_back(r2.a.copy());
        }
        store(Variable(arr));
        break;
    }
    case HSH2('a','n'): { /// (an) -> a: every nth element (negative n = reverse)
        Retrieval<tArray*, tNum> r(this, consume);
        int n = round(r.b);
        bool rev = n < 0;
        auto arr = new tArray;
        for (int i = (rev ? (r.a->size()-1) : 0); rev ? (i >= 0) :
                (((vvs)i) < r.a->size()); i += n) {
            arr->push_back((*r.a)[(vvs)i]);
        }
        store(Variable(arr));
        break;
    }
    case HSH3('A','S','E'): { /// (ab) -> a: select
        Retrieval<tArray*, std::string*> r(this, consume);
        auto arr = new tArray;
        for (Variable v : *r.a) {
            store(v);
            run(*r.b);
            // WARNING: do *not* try to "optimize" this into
            //   if (Retrieval<bool>(this).b) ...
            // that fails on some edge-cases, such as
            //   ("test":2nB;aM:;AsE
            // where the predicate returns its own argument that is of a
            // pointer type. If the Retrieval is within the if parens, its
            // destructor gets called *before* v.copy() is run, and the pointer
            // that v refers to has already been delete'd
            Retrieval<bool> r2(this);
            if (r2.b) arr->push_back(v.copy());
        }
        store(Variable(arr));
        break;
    }
    case HSH3('A','S','I'): { /// (ab) -> a: select by index / index of / find index
        Retrieval<tArray*, std::string*> r(this, consume);
        auto arr = new tArray;
        for (vvs i = 0; i < r.a->size(); ++i) {
            Variable v = (*r.a)[i];
            store(v);
            run(*r.b);
            if (Retrieval<bool>(this).b) arr->push_back(Variable((tNum)i));
        }
        store(Variable(arr));
        break;
    }
    case HSH3('A','A','L'): { /// (an) -> a: elements at indeces less than n
        Retrieval<tArray*, tNum> r(this, consume);
        vvs n = round(r.b);
        auto arr = new tArray;
        for (vvs i = 0; i < r.a->size() && i < n; ++i) arr->push_back((*r.a)[i]);
        store(Variable(arr));
        break;
    }
    case HSH3('A','A','G'): { /// (an) -> a: elements at indeces greater than n
        Retrieval<tArray*, tNum> r(this, consume);
        int n = round(r.b);
        auto arr = new tArray;
        for (vvs i = n + 1; i < r.a->size(); ++i) arr->push_back((*r.a)[i]);
        store(Variable(arr));
        break;
    }
    case HSH2('a','a'): { /// (an) -> *: element at index
        Retrieval<tArray*, tNum> r(this, consume);
        try {
            store(r.a->at((int)r.b));
        } catch (std::out_of_range& oor) {
            store(Variable(0.0));  // this is just arbitrary
        }
        break;
    }
    case HSH2('a','l'): { /// (a) -> n: array length
        Retrieval<tArray*> r(this, consume);
        store(Variable((tNum)r.a->size()));
        break;
    }
    case HSH2('a','z'): { /// (a) -> a: zip/transpose
        Retrieval<tArray*> r(this, consume);
        auto arr = new tArray;
        // sanity check, also get max size
        vvs maxSize = 0;
        for (vvs i = 0; i < r.a->size(); ++i) {
            if ((*r.a)[i].type != Variable::ARRAY) {
                throw SnowmanException("at az: array elements are not arrays, "
                        "stopping execution of az", false);
            }
            if ((*r.a)[i].arrayVal->size() > maxSize) {
                maxSize = (*r.a)[i].arrayVal->size();
            }
        }
        // fill arr now
        for (vvs j = 0; j < maxSize; ++j) {
            auto tmp = new tArray;
            for (vvs i = 0; i < r.a->size(); ++i) {
                if ((*r.a)[i].arrayVal->size() > j) {
                    tmp->push_back((*(*r.a)[i].arrayVal)[j]);
                }
            }
            arr->push_back(Variable(tmp));
        }
        store(Variable(arr));
        break;
    }
    case HSH3('A','S','P'): { /// (anna) -> a: splice (first argument is array to splice, second is start index, third is length, fourth is what to replace with)
        Retrieval<tArray*, tNum, tNum, tArray*> r(this, consume);
        vvs idx = round(r.b), len = round(r.c);
        auto arr = new tArray;
        for (vvs i = 0; i < idx && i < r.a->size(); ++i) {
            arr->push_back((*r.a)[i]);
        }
        arr->insert(arr->end(), r.d->begin(), r.d->end());
        for (vvs i = idx + len; i < r.a->size(); ++i) {
            arr->push_back((*r.a)[i]);
        }
        store(Variable(arr));
        break;
    }
    case HSH3('A','F','L'): { /// (an) -> a: flatten (number is how many "layers" to flatten; 0 means completely flatten the array)
        Retrieval<tArray*, tNum> r(this, consume);
        int count = round(r.b);
        bool infinite = (count == 0), changed = true;
        while ((changed) && (infinite || count--)) {
            changed = false;
            tArray arr;
            for (Variable v : *r.a) {
                if (v.type == Variable::ARRAY) {
                    changed = true;
                    for (Variable v2 : *v.arrayVal) {
                        arr.push_back(v2);
                    }
                } else {
                    arr.push_back(v);
                }
            }
            *r.a = arr;
        }
        store(Variable(new tArray(*r.a)));
        break;
    }
    case HSH3('A','S','H'): { /// (a) -> a: shuffle array
        Retrieval<tArray*> r(this, consume);
        std::random_shuffle(r.a->begin(), r.a->end());
        store(Variable(new tArray(*r.a)));
        break;
    }

    /// "String" operators
    case HSH2('s','b'): { /// (an) -> n: from-base from array-"string"
        Retrieval<tArray*, tNum> r(this, consume);
        std::string str = arrToString(*r.a);
        int base = round(r.b);
        tNum num = 0;
        bool neg = false;
        if (str[0] == '-') {
            neg = true;
            str = str.substr(1);
        }
        ss dotPos = str.find('.');
        int subPos = str.length() - 1;
        if (dotPos != std::string::npos) {
            str.erase(dotPos, 1);
            subPos = dotPos - 1;
        }
        for (int i = str.length() - 1; i >= 0; --i) {
            char c = (str[i] >= 'A' && str[i] <= 'Z') ? str[i] + ('a' - 'A') :
                str[i];
            ss digit = DIGITS.find(c);
            if ((digit == std::string::npos) || (digit >= (ss) base)) {
                num = 0;
                break;
            } else {
                num += digit * pow(base, subPos - i);
            }
        }
        store(Variable(num * (neg ? -1 : 1)));
        break;
    }
    case HSH2('s','p'): { /// (a) -> -: print an array-"string"
        Retrieval<tArray*> r(this, consume);
        std::cout << arrToString(*r.a);
        break;
    }
#ifndef OMIT_REGEX
    case HSH2('s','m'): { /// (aa) -> a: regex match; first array-"string" is search text, second array-"string" is regex
        Retrieval<tArray*, tArray*> r(this, consume);
        std::string str = arrToString(*r.a);
        std::regex rgx;
        try {
            rgx = std::regex(arrToString(*r.b), std::regex::extended);
        } catch (std::regex_error& re) {
            throw SnowmanException("at sm: regex error, stopping execution of "
                "sm", false);
        }
        auto mb = std::sregex_iterator(str.begin(), str.end(), rgx),
             me = std::sregex_iterator();
        auto arr = new tArray;
        for (auto it = mb; it != me; ++it) {
            arr->push_back(stringToArr(it->str()));
        }
        store(Variable(arr));
        break;
    }
    case HSH2('s','r'): { /// (aaa) -> a: regex replace; first array-"string" is string to operate on, second array-"string" is rege, third is replacement text
        Retrieval<tArray*, tArray*, tArray*> r(this, consume);
        std::string str = arrToString(*r.a);
        std::regex rgx;
        try {
            rgx = std::regex(arrToString(*r.b), std::regex::extended);
        } catch (std::regex_error& re) {
            throw SnowmanException("at sr: regex error, stopping execution of "
                "sr", false);
        }
        std::string repl = arrToString(*r.c);
        store(Variable(stringToArr(std::regex_replace(str, rgx, repl))));
        break;
    }
    case HSH3('S','R','B'): { /// (aab) -> a: same as `sr` but with a block instead of array-"string"
        Retrieval<tArray*, tArray*, std::string*> r(this, consume);
        std::string str = arrToString(*r.a);
        std::regex rgx;
        try {
            rgx = std::regex(arrToString(*r.b), std::regex::extended);
        } catch (std::regex_error& re) {
            throw SnowmanException("at srb: regex error, stopping execution of "
                "srb", false);
        }
        std::string repl = *r.c;
        auto rb = std::sregex_token_iterator(str.begin(), str.end(), rgx, {-1,0}),
             re = std::sregex_token_iterator();
        std::string result;
        bool isMatch = false;
        for (auto it = rb; it != re; ++it) {
            if (isMatch) {
                store(stringToArr(*it));
                run(repl);
                Retrieval<tArray*> r2(this, true);
                result += arrToString(*r2.a);
            } else {
                result += *it;
            }
            isMatch = !isMatch;
        }
        store(stringToArr(result));
        break;
    }
#endif

    /// Block operators
    case HSH2('b','r'): { /// (bn) -> -: repeat
        Retrieval<std::string*, tNum> r(this, consume);
        for (int i = 0; i < round(r.b); ++i) run(*r.a);
        break;
    }
    case HSH2('b','w'): { /// (bb) -> -: while ("returned" value from second block is simply first non-undefined active variable, which is set to undefined after reading it)
        Retrieval<std::string*, std::string*> r(this, consume);
        while (1) {
            run(*r.b);
            if (!Retrieval<bool>(this).b) break;
            run(*r.a);
        }
        break;
    }
    case HSH2('b','i'): { /// (bb*) -> -: if/else
        Retrieval<std::string*, std::string*, Variable> r(this, consume);
        if (Snowman::toBool(r.c)) run(*r.a);
        else run(*r.b);
        break;
    }
    case HSH2('b','d'): { /// (b) -> -: do (`:...;bD` is basically the same as `:;:...;bW`)
        Retrieval<std::string*> r(this, consume);
        do {
            run(*r.a);
        } while (Retrieval<bool>(this).b);
        break;
    }
    case HSH2('b','e'): { /// (b) -> -: execute / evaluate
        Retrieval<std::string*> r(this, consume);
        run(*r.a);
        break;
    }

    /// (Any type) operators
    case HSH2('n','o'): { /// (*) -> n: boolean/logical not (returns `1` for `0 :; []`, `0` otherwise)
        Retrieval<Variable> r(this, consume);
        store(Variable((tNum)(!Snowman::toBool(r.a))));
        break;
    }
    case HSH2('w','r'): { /// (*) -> a: wrap in array
        Retrieval<Variable> r(this, consume);
        auto arr = new tArray(1);
        (*arr)[0] = r.a.copy();
        store(Variable(arr));
        break;
    }
    case HSH2('t','s'): { /// (*) -> a: to array-"string"
        Retrieval<Variable> r(this, consume);
        store(stringToArr(Snowman::inspect(r.a)));
        break;
    }
    case HSH2('b','o'): { /// (**) -> n: boolean/logical and ("bo" = "both" because "an," "ad," and "nd" are all taken)
        Retrieval<Variable, Variable> r(this, consume);
        store(Variable((tNum)(Snowman::toBool(r.a) && Snowman::toBool(r.b))));
        break;
    }
    case HSH2('o','r'): { /// (**) -> n: boolean/logical or
        Retrieval<Variable, Variable> r(this, consume);
        store(Variable((tNum)(Snowman::toBool(r.a) || Snowman::toBool(r.b))));
        break;
    }
    case HSH2('e','q'): { /// (**) -> n: equal?
        Retrieval<Variable, Variable> r(this, consume);
        if (r.a.type != r.b.type) {
            store(Variable(0.0));
        } else {
            switch (r.a.type) {
            case Variable::UNDEFINED:
                store(Variable(1.0));
                break;
            case Variable::NUM:
                store(Variable((tNum)(r.a.numVal == r.b.numVal)));
                break;
            case Variable::ARRAY:
                store(Variable((tNum)((*r.a.arrayVal) == (*r.b.arrayVal))));
                break;
            case Variable::BLOCK:
                store(Variable((tNum)((*r.a.blockVal) == (*r.b.blockVal))));
                break;
            }
        }
        break;
    }
    case HSH2('d','u'): { /// (*) -> **: duplicate
        Retrieval<Variable> r(this, consume);
        store(r.a.copy());
        store(r.a.copy());
        break;
    }

    /// "Void" operators
    case HSH2('v','n'): /// (-) -> -: no-op (do nothing)
        break;
    case HSH2('v','g'): { /// (-) -> a: get line of input (as an array-"string")
        std::string line;
        std::getline(std::cin, line);
        store(stringToArr(line));
        break;
    }
    case HSH2('v','r'): /// (-) -> n: random number [0,1)
        store(Variable((tNum)rand() / RAND_MAX));
        break;
    case HSH2('v','t'): /// (-) -> n: time (milliseconds since epoch)
        store(Variable((tNum)std::chrono::duration_cast
            <std::chrono::milliseconds>(std::chrono::system_clock::now()
                .time_since_epoch()).count()));
        break;
    case HSH2('v','a'): /// (-) -> a: get command line args
        store(Variable(new tArray(args)));
        break;

    default:
        throw SnowmanException("at evalToken: unrecognized token?", true);

    }
}

void Snowman::store(Variable val) {
    // for definition of "store", see doc/snowman.md
    for (int i = 0; i < 8; ++i) {
        if (activeVars[i] && vars[i].type == Variable::UNDEFINED) {
            vars[i] = val;
            return;
        }
    }
    val.mm();
}

Variable Snowman::retrieve(int type, bool consume, int skip) {
    // for definition of "retrieve", see doc/snowman.md
    // (also used for gathering letter operator arguments)
    // default value of consume is true
    // default value of skip is 0
    // if skip is -1, any amount of variables will be skipped (ex. retrieve(-1,
    //   false, -1) will get you the first non-undefined variable)
    for (int i = 0; i < 8; ++i) {
        if (activeVars[i]) {
            if (skip > 0) {
                --skip;
                continue;
            }
            if ((vars[i].type != Variable::UNDEFINED) &&
                    (type == -1 || vars[i].type == type)) {
                if (consume) {
                    Variable v = vars[i];
                    vars[i].type = Variable::UNDEFINED;
                    return v;
                }
                return vars[i];
            } else if (skip == -1) {
                continue;
            } else {
                throw SnowmanException("at retrieve: wrong type, stopping "
                    "execution of operator", false);
            }
        }
    }
    throw SnowmanException("at retrieve: not enough variables, stopping "
        "execution of operator", true);
}

std::string Snowman::arrToString(tArray arr) {
    // convert std::vector<Variable[.type==Variable::NUM]> to std::string
    std::string s;
    for (Variable v : arr) {
        if (v.type == Variable::NUM) {
            s += (char)v.numVal;
        } else {
            throw SnowmanException("at arrToString: bad argument?", true);
        }
    }
    return s;
}

Variable Snowman::stringToArr(std::string str) {
    auto arr = new tArray;
    for (char& c : str) {
        arr->push_back(Variable((tNum)c));
    }
    return Variable(arr);
}

std::string Snowman::inspect(Variable v) {
    switch (v.type) {
    case Variable::UNDEFINED:
        return "";
    case Variable::NUM: {
        char buf[64];
        sprintf(buf, "%.*G", 16, v.numVal);
        return std::string(buf);
    }
    case Variable::ARRAY: {
        std::string s = "[";
        for (Variable v2 : *v.arrayVal) {
            s += Snowman::inspect(v2) + " ";
        }
        if (s.length() == 1) s = "[]";
        else s[s.length()-1] = ']';
        return s;
    }
    case Variable::BLOCK:
        return ":" + (*v.blockVal) + ";";
    default: throw SnowmanException("at inspect: impossible type?", true);
    }
}

bool Snowman::toBool(Variable v) {
    switch (v.type) {
    case Variable::UNDEFINED:
        return false;
    case Variable::NUM:
        return v.numVal != 0;
    case Variable::ARRAY:
        return (*v.arrayVal).size() != 0;
    case Variable::BLOCK:
        return (*v.blockVal).size() != 0;
    default: throw SnowmanException("at toBool: impossible type?", true);
    }
}

std::string Snowman::debug() {
    std::string s;
    for (int i = 0; i < 8; ++i) {
        s += "{" + (activeVars[i] ? std::string("*") : std::string("")) + " " +
            Snowman::inspect(vars[i]) + " } ";
    }

    for (const auto& pv : permavars) {
        s += std::string(pv.first / 2, '=') + (pv.first % 2 == 0 ? "+" : "!") +
            "=" + Snowman::inspect(pv.second) + " ";
    }

    s[s.length()-1] = '\n';
    return s;
}

void Snowman::addArg(std::string arg) {
    args.push_back(stringToArr(arg));
}
