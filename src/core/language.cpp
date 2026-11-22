#include "core/language.hpp"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <vector>

namespace neko::pad {

namespace {

[[nodiscard]] QStringList words(const char* list) {
    return QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

// Word lists of the C family, shared by several entries.
constexpr const char* kCKeywords =
    "auto break case const continue default do else enum extern for goto if inline register restrict return "
    "sizeof static struct switch typedef union volatile while _Alignas _Alignof _Atomic _Generic _Noreturn "
    "_Static_assert _Thread_local alignas alignof constexpr static_assert thread_local typeof";
constexpr const char* kCTypes =
    "bool char double float int long short signed unsigned void size_t ssize_t ptrdiff_t intptr_t uintptr_t "
    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t wchar_t char8_t char16_t char32_t FILE";
constexpr const char* kCppKeywords =
    "alignas alignof and and_eq asm auto bitand bitor break case catch class co_await co_return co_yield compl "
    "concept const const_cast consteval constexpr constinit continue decltype default delete do dynamic_cast "
    "else enum explicit export extern final for friend goto if import inline module mutable namespace new "
    "noexcept not not_eq operator or or_eq override private protected public register reinterpret_cast "
    "requires return sizeof static static_assert static_cast struct switch template this thread_local throw "
    "try typedef typeid typename union using virtual volatile while xor xor_eq";
constexpr const char* kCppTypes =
    "bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t "
    "ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t std string "
    "string_view vector array map unordered_map set unordered_set optional variant expected span unique_ptr "
    "shared_ptr weak_ptr function pair tuple QString QStringView QByteArray QList QVector QHash QMap QObject "
    "QWidget";
constexpr const char* kCConstants = "true false nullptr NULL TRUE FALSE EOF";

[[nodiscard]] std::vector<Language> buildLanguages() {
    std::vector<Language> list;
    auto add = [&](Language language) {
        list.push_back(std::move(language));
    };

    {
        Language l;
        l.id = QStringLiteral("text");
        l.name = QCoreApplication::translate("neko::pad::Language", "Normal Text");
        l.family = LanguageFamily::Plain;
        l.extensions = words("txt text log");
        l.quotes.clear();
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("c");
        l.name = QStringLiteral("C");
        l.family = LanguageFamily::Code;
        l.extensions = words("c h");
        l.keywords = words(kCKeywords);
        l.types = words(kCTypes);
        l.constants = words(kCConstants);
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.preprocessor = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("cpp");
        l.name = QStringLiteral("C++");
        l.family = LanguageFamily::Code;
        l.extensions = words("cpp cxx cc c++ hpp hxx hh h++ ipp inl tpp ixx cppm mpp ino cu cuh");
        l.keywords = words(kCppKeywords);
        l.types = words(kCppTypes);
        l.constants = words(kCConstants);
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.preprocessor = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("csharp");
        l.name = QStringLiteral("C#");
        l.family = LanguageFamily::Code;
        l.extensions = words("cs csx");
        l.keywords = words(
            "abstract as async await base break case catch checked class const continue default delegate do else "
            "enum event explicit extern finally fixed for foreach get goto if implicit in init interface internal "
            "is lock namespace new operator out override params partial private protected public readonly record "
            "ref required return sealed set sizeof stackalloc static struct switch this throw try typeof unchecked "
            "unsafe using value var virtual volatile when where while with yield");
        l.types = words(
            "bool byte char decimal double dynamic float int long nint nuint object sbyte short string uint ulong "
            "ushort void String Object List Dictionary Task IEnumerable Span");
        l.constants = words("true false null");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.preprocessor = true;
        l.annotations = false;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("java");
        l.name = QStringLiteral("Java");
        l.family = LanguageFamily::Code;
        l.extensions = words("java");
        l.keywords = words(
            "abstract assert break case catch class const continue default do else enum exports extends final "
            "finally for goto if implements import instanceof interface module native new non-sealed open opens "
            "package permits private protected provides public record requires return sealed static strictfp "
            "super switch synchronized this throw throws to transient transitive try uses var void volatile when "
            "while with yield");
        l.types = words("boolean byte char double float int long short String Object Integer List Map");
        l.constants = words("true false null");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.annotations = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("kotlin");
        l.name = QStringLiteral("Kotlin");
        l.family = LanguageFamily::Code;
        l.extensions = words("kt kts");
        l.keywords = words(
            "abstract annotation as break by catch class companion const constructor continue crossinline data do "
            "else enum expect external final finally for fun get if import in infix init inline inner interface "
            "internal is lateinit noinline object open operator out override package private protected public "
            "reified return sealed set super suspend tailrec this throw try typealias val var vararg when where "
            "while");
        l.types = words("Any Boolean Byte Char Double Float Int Long Nothing Short String Unit List Map");
        l.constants = words("true false null");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.annotations = true;
        l.tripleQuotes = true;
        l.quotes = QStringLiteral("\"'");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("javascript");
        l.name = QStringLiteral("JavaScript");
        l.family = LanguageFamily::Code;
        l.extensions = words("js mjs cjs jsx");
        l.shebangs = words("node deno bun");
        l.keywords = words(
            "async await break case catch class const continue debugger default delete do else export extends "
            "finally for from function get if import in instanceof let new of return set static super switch this "
            "throw try typeof var void while with yield");
        l.types =
            words("Array Boolean Date Error Function JSON Map Math Number Object Promise RegExp Set String Symbol");
        l.constants = words("true false null undefined NaN Infinity");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.quotes = QStringLiteral("\"'`");
        l.backtickMultiline = true;
        l.extraIdentChars = QStringLiteral("$");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("typescript");
        l.name = QStringLiteral("TypeScript");
        l.family = LanguageFamily::Code;
        l.extensions = words("ts mts cts tsx");
        l.keywords = words(
            "abstract as async await break case catch class const constructor continue debugger declare default "
            "delete do else enum export extends finally for from function get if implements import in infer "
            "instanceof interface is keyof let module namespace new of override private protected public readonly "
            "return satisfies set static super switch this throw try type typeof var void while with yield");
        l.types = words(
            "any bigint boolean never number object string symbol unknown void Array Map Promise Record Partial Set");
        l.constants = words("true false null undefined NaN Infinity");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.quotes = QStringLiteral("\"'`");
        l.backtickMultiline = true;
        l.annotations = true;
        l.extraIdentChars = QStringLiteral("$");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("json");
        l.name = QStringLiteral("JSON");
        l.family = LanguageFamily::Code;
        l.extensions = words("json jsonc json5 geojson webmanifest");
        l.fileNames = words(".babelrc .eslintrc .prettierrc");
        l.constants = words("true false null");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.quotes = QStringLiteral("\"");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("go");
        l.name = QStringLiteral("Go");
        l.family = LanguageFamily::Code;
        l.extensions = words("go");
        l.keywords = words(
            "break case chan const continue default defer else fallthrough for func go goto if import interface "
            "map package range return select struct switch type var");
        l.types = words(
            "any bool byte comparable complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
            "string uint uint8 uint16 uint32 uint64 uintptr");
        l.constants = words("true false nil iota");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.quotes = QStringLiteral("\"'`");
        l.backtickMultiline = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("rust");
        l.name = QStringLiteral("Rust");
        l.family = LanguageFamily::Code;
        l.extensions = words("rs");
        l.keywords = words(
            "as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod "
            "move mut pub ref return self Self static struct super trait type union unsafe use where while yield");
        l.types = words(
            "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize Box Option Result String Vec");
        l.constants = words("true false None Some Ok Err");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.quotes = QStringLiteral("\"");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("swift");
        l.name = QStringLiteral("Swift");
        l.family = LanguageFamily::Code;
        l.extensions = words("swift");
        l.keywords = words(
            "actor as associatedtype async await break case catch class continue default defer deinit do else enum "
            "extension fallthrough fileprivate final for func guard if import in init inout internal is lazy let "
            "mutating nonisolated open operator override private protocol public repeat rethrows return self Self "
            "some static struct subscript super switch throw throws try typealias var weak where while");
        l.types = words("Any Bool Character Double Float Int String UInt Void Array Dictionary Optional Set");
        l.constants = words("true false nil");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.quotes = QStringLiteral("\"");
        l.tripleQuotes = true;
        l.annotations = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("python");
        l.name = QStringLiteral("Python");
        l.family = LanguageFamily::Code;
        l.extensions = words("py pyw pyi pyx pxd");
        l.fileNames = words("SConstruct SConscript");
        l.shebangs = words("python python2 python3 pypy pypy3");
        l.keywords = words(
            "and as assert async await break case class continue def del elif else except finally for from global "
            "if import in is lambda match nonlocal not or pass raise return try type while with yield");
        l.types =
            words("bool bytearray bytes complex dict float frozenset int list object set str tuple type self cls");
        l.constants = words("True False None NotImplemented Ellipsis __name__ __file__");
        l.lineComment = QStringLiteral("#");
        l.tripleQuotes = true;
        l.annotations = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("ruby");
        l.name = QStringLiteral("Ruby");
        l.family = LanguageFamily::Code;
        l.extensions = words("rb rake gemspec ru erb");
        l.fileNames = words("Rakefile Gemfile Vagrantfile Podfile");
        l.shebangs = words("ruby");
        l.keywords = words(
            "BEGIN END alias and begin break case class def defined? do else elsif end ensure for if in module next "
            "not or redo rescue retry return self super then undef unless until when while yield require "
            "require_relative attr_accessor attr_reader attr_writer");
        l.constants = words("true false nil");
        l.lineComment = QStringLiteral("#");
        l.variables = false;
        l.annotations = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("php");
        l.name = QStringLiteral("PHP");
        l.family = LanguageFamily::Code;
        l.extensions = words("php php3 php4 php5 phtml phps");
        l.shebangs = words("php");
        l.keywords = words(
            "abstract and array as break callable case catch class clone const continue declare default do echo "
            "else elseif empty enddeclare endfor endforeach endif endswitch endwhile enum extends final finally fn "
            "for foreach function global goto if implements include include_once instanceof insteadof interface "
            "isset list match namespace new or print private protected public readonly require require_once return "
            "static switch throw trait try unset use var while xor yield");
        l.types = words("bool int float string array object mixed void never iterable self parent");
        l.constants = words("true false null TRUE FALSE NULL");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.variables = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("perl");
        l.name = QStringLiteral("Perl");
        l.family = LanguageFamily::Code;
        l.extensions = words("pl pm t pod");
        l.shebangs = words("perl");
        l.keywords = words(
            "if elsif else unless while until for foreach do last next redo return my our local sub package use no "
            "require eval die print printf say open close and or not eq ne lt gt le ge cmp");
        l.lineComment = QStringLiteral("#");
        l.variables = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("lua");
        l.name = QStringLiteral("Lua");
        l.family = LanguageFamily::Code;
        l.extensions = words("lua");
        l.shebangs = words("lua luajit");
        l.keywords =
            words("and break do else elseif end for function goto if in local not or repeat return then until while");
        l.constants = words("true false nil");
        l.lineComment = QStringLiteral("--");
        l.blockStart = QStringLiteral("--[[");
        l.blockEnd = QStringLiteral("]]");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("shell");
        l.name = QStringLiteral("Shell");
        l.family = LanguageFamily::Code;
        l.extensions = words("sh bash zsh ksh fish command");
        l.fileNames = words(".bashrc .bash_profile .profile .zshrc .zprofile PKGBUILD");
        l.shebangs = words("sh bash zsh ksh dash fish");
        l.keywords = words(
            "if then else elif fi case esac for select while until do done in function time coproc return exit "
            "break continue local export readonly declare typeset unset shift source alias set trap eval exec");
        l.types = words("echo printf read cd pwd test");
        l.constants = words("true false");
        l.lineComment = QStringLiteral("#");
        l.variables = true;
        l.quotes = QStringLiteral("\"'`");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("powershell");
        l.name = QStringLiteral("PowerShell");
        l.family = LanguageFamily::Code;
        l.extensions = words("ps1 psm1 psd1");
        l.shebangs = words("pwsh powershell");
        l.keywords = words(
            "begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally "
            "for foreach from function hidden if in param process return static switch throw trap try until using "
            "var while");
        l.constants = words("$true $false $null");
        l.lineComment = QStringLiteral("#");
        l.blockStart = QStringLiteral("<#");
        l.blockEnd = QStringLiteral("#>");
        l.caseInsensitive = true;
        l.variables = true;
        l.extraIdentChars = QStringLiteral("-");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("batch");
        l.name = QStringLiteral("Batch");
        l.family = LanguageFamily::Code;
        l.extensions = words("bat cmd nt");
        l.keywords = words(
            "assoc break call cd chdir cls color copy date del dir do echo else endlocal erase exist exit for ftype "
            "goto if in md mkdir move not path pause popd prompt pushd rd rem ren rename rmdir set setlocal shift "
            "start time title type ver verify vol errorlevel defined equ neq lss leq gtr geq");
        l.lineComment = QStringLiteral("REM");
        l.caseInsensitive = true;
        l.variables = true;
        l.quotes = QStringLiteral("\"");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("sql");
        l.name = QStringLiteral("SQL");
        l.family = LanguageFamily::Code;
        l.extensions = words("sql ddl dml");
        l.keywords = words(
            "add all alter and any as asc begin between by case cascade check column commit constraint create cross "
            "database default delete desc distinct drop else end except exists foreign from full group having if in "
            "index inner insert intersect into is join key left like limit not null offset on or order outer "
            "primary procedure references replace returning right rollback select set table then to transaction "
            "trigger truncate union unique update using values view when where with window over partition");
        l.types = words(
            "bigint binary bit blob boolean char date datetime decimal double float int integer interval json "
            "jsonb nchar numeric nvarchar real serial smallint text time timestamp tinyint uuid varbinary varchar");
        l.constants = words("true false null");
        l.lineComment = QStringLiteral("--");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.caseInsensitive = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("cmake");
        l.name = QStringLiteral("CMake");
        l.family = LanguageFamily::Code;
        l.extensions = words("cmake");
        l.fileNames = words("CMakeLists.txt");
        l.keywords = words(
            "add_compile_definitions add_compile_options add_custom_command add_custom_target add_definitions "
            "add_dependencies add_executable add_library add_subdirectory add_test block break cmake_minimum_required "
            "cmake_parse_arguments cmake_path configure_file continue else elseif enable_testing endblock "
            "endforeach endfunction endif endmacro endwhile execute_process file find_library find_package "
            "find_path find_program foreach function get_filename_component get_property get_target_property if "
            "include include_directories install list macro message option project return set set_property "
            "set_target_properties string target_compile_definitions target_compile_features "
            "target_compile_options target_include_directories target_link_libraries target_link_options "
            "target_sources unset while fetchcontent_declare fetchcontent_makeavailable");
        l.constants = words(
            "on off true false yes no and or not defined exists equal strequal matches public private interface "
            "required components");
        l.lineComment = QStringLiteral("#");
        l.caseInsensitive = true;
        l.variables = true;
        l.quotes = QStringLiteral("\"");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("makefile");
        l.name = QStringLiteral("Makefile");
        l.family = LanguageFamily::Code;
        l.extensions = words("mk mak make");
        l.fileNames = words("Makefile makefile GNUmakefile");
        l.keywords =
            words("ifeq ifneq ifdef ifndef else endif include override export unexport define endef vpath .PHONY");
        l.lineComment = QStringLiteral("#");
        l.variables = true;
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("yaml");
        l.name = QStringLiteral("YAML");
        l.family = LanguageFamily::Code;
        l.extensions = words("yaml yml");
        l.fileNames = words(".clang-format .clang-tidy .clangd");
        l.constants = words("true false null yes no on off True False Null Yes No On Off ~");
        l.lineComment = QStringLiteral("#");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("css");
        l.name = QStringLiteral("CSS");
        l.family = LanguageFamily::Code;
        l.extensions = words("css scss less");
        l.keywords = words(
            "@media @import @font-face @keyframes @supports @layer @container @charset @namespace @page !important");
        l.constants = words(
            "auto none inherit initial unset block inline flex grid absolute relative fixed sticky hidden visible "
            "solid dashed dotted bold normal italic transparent");
        l.lineComment = QStringLiteral("//");
        l.blockStart = QStringLiteral("/*");
        l.blockEnd = QStringLiteral("*/");
        l.extraIdentChars = QStringLiteral("-@!");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("xml");
        l.name = QStringLiteral("XML");
        l.family = LanguageFamily::Markup;
        l.extensions = words(
            "xml xsd xsl xslt svg plist qrc ui ts csproj vcxproj props targets nuspec resx xaml axaml kml gpx rss "
            "atom wsdl");
        l.blockStart = QStringLiteral("<!--");
        l.blockEnd = QStringLiteral("-->");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("html");
        l.name = QStringLiteral("HTML");
        l.family = LanguageFamily::Markup;
        l.extensions = words("html htm xhtml shtml vue svelte");
        l.blockStart = QStringLiteral("<!--");
        l.blockEnd = QStringLiteral("-->");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("markdown");
        l.name = QStringLiteral("Markdown");
        l.family = LanguageFamily::Markdown;
        l.extensions = words("md markdown mdown mkd mkdn mdx");
        l.blockStart = QStringLiteral("<!--");
        l.blockEnd = QStringLiteral("-->");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("ini");
        l.name = QStringLiteral("INI / TOML");
        l.family = LanguageFamily::Ini;
        l.extensions = words("ini cfg conf toml properties desktop inf reg editorconfig gitconfig service");
        l.fileNames = words(".editorconfig .gitconfig .gitmodules Cargo.lock");
        l.lineComment = QStringLiteral("#");
        add(std::move(l));
    }
    {
        Language l;
        l.id = QStringLiteral("diff");
        l.name = QStringLiteral("Diff");
        l.family = LanguageFamily::Diff;
        l.extensions = words("diff patch rej");
        add(std::move(l));
    }

    // Sorted word lists, lower case for the case-insensitive languages: containsWord()
    // searches them by bisection.
    for (Language& l : list) {
        for (QStringList* wordList : {&l.keywords, &l.types, &l.constants}) {
            if (l.caseInsensitive) {
                for (QString& w : *wordList) {
                    w = w.toLower();
                }
            }
            std::ranges::sort(*wordList);
            wordList->erase(std::unique(wordList->begin(), wordList->end()), wordList->end());
        }
    }
    // "Normal Text" first, the others by name.
    std::sort(list.begin() + 1, list.end(), [](const Language& a, const Language& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return list;
}

struct Registry {
    std::vector<Language> list = buildLanguages();
    QHash<QString, const Language*> byExtension;
    QHash<QString, const Language*> byFileName; ///< lower case
    QHash<QString, const Language*> byShebang;

    Registry() {
        for (const Language& l : list) {
            for (const QString& e : l.extensions) {
                byExtension.insert(e, &l);
            }
            for (const QString& n : l.fileNames) {
                byFileName.insert(n.toLower(), &l);
            }
            for (const QString& s : l.shebangs) {
                byShebang.insert(s, &l);
            }
        }
    }
};

const Registry& registry() {
    static const Registry r;
    return r;
}

} // namespace

std::span<const Language> languages() {
    return registry().list;
}

const Language& plainTextLanguage() {
    return registry().list.front();
}

const Language* languageById(QStringView id) {
    for (const Language& l : registry().list) {
        if (l.id == id) {
            return &l;
        }
    }
    return nullptr;
}

const Language* languageForFileName(QStringView fileName) {
    const qsizetype slash = std::max(fileName.lastIndexOf(u'/'), fileName.lastIndexOf(u'\\'));
    const QStringView name = fileName.sliced(slash + 1);
    if (name.isEmpty()) {
        return nullptr;
    }
    const Registry& r = registry();
    if (const auto it = r.byFileName.constFind(name.toString().toLower()); it != r.byFileName.cend()) {
        return it.value();
    }
    const qsizetype dot = name.lastIndexOf(u'.');
    if (dot < 0 || dot + 1 >= name.size()) {
        return nullptr;
    }
    const QString extension = name.sliced(dot + 1).toString().toLower();
    if (const auto it = r.byExtension.constFind(extension); it != r.byExtension.cend()) {
        return it.value();
    }
    return nullptr;
}

const Language* languageForFirstLine(QStringView firstLine) {
    const QStringView line = firstLine.trimmed();
    if (line.startsWith(u"#!")) {
        // "#!/usr/bin/env -S python3 -u" -> "python3"; "#!/bin/bash" -> "bash"
        const QList<QStringView> parts = line.sliced(2).split(u' ', Qt::SkipEmptyParts);
        for (QStringView part : parts) {
            if (part.startsWith(u'-')) {
                continue;
            }
            const qsizetype slash = part.lastIndexOf(u'/');
            const QStringView program = part.sliced(slash + 1);
            if (program == u"env") {
                continue;
            }
            const Registry& r = registry();
            QString key = program.toString();
            // python3.12 -> python3
            while (!key.isEmpty() && !r.byShebang.contains(key) && (key.back().isDigit() || key.back() == u'.')) {
                key.chop(1);
            }
            if (const auto it = r.byShebang.constFind(key); it != r.byShebang.cend()) {
                return it.value();
            }
            return nullptr;
        }
        return nullptr;
    }
    if (line.startsWith(u"<?xml")) {
        return languageById(u"xml");
    }
    if (line.startsWith(u"<!DOCTYPE html", Qt::CaseInsensitive) || line.startsWith(u"<html", Qt::CaseInsensitive)) {
        return languageById(u"html");
    }
    if (line.startsWith(u"<?php")) {
        return languageById(u"php");
    }
    if (line.startsWith(u"diff --git ") || line.startsWith(u"--- ")) {
        return languageById(u"diff");
    }
    return nullptr;
}

const Language& detectLanguage(QStringView fileName, QStringView firstLine) {
    if (const Language* l = languageForFileName(fileName)) {
        return *l;
    }
    if (const Language* l = languageForFirstLine(firstLine)) {
        return *l;
    }
    return plainTextLanguage();
}

bool containsWord(const QStringList& sorted, QStringView word, bool caseInsensitive) {
    if (sorted.isEmpty()) {
        return false;
    }
    auto lookup = [&](QStringView w) {
        const auto it = std::lower_bound(sorted.cbegin(), sorted.cend(), w, [](const QString& a, QStringView b) {
            return QStringView(a).compare(b) < 0;
        });
        return it != sorted.cend() && QStringView(*it) == w;
    };
    if (!caseInsensitive) {
        return lookup(word);
    }
    // Keywords are short: lower-case into a stack buffer.
    char16_t buffer[64];
    if (word.size() > static_cast<qsizetype>(std::size(buffer))) {
        return false;
    }
    for (qsizetype i = 0; i < word.size(); ++i) {
        buffer[i] = word[i].toLower().unicode();
    }
    return lookup(QStringView(buffer, word.size()));
}

QString fileDialogFilter() {
    QStringList filters;
    filters << QCoreApplication::translate("neko::pad::Language", "All files (*)");
    for (const Language& l : languages()) {
        QStringList patterns;
        for (const QString& e : l.extensions) {
            patterns << QStringLiteral("*.") + e;
        }
        for (const QString& n : l.fileNames) {
            patterns << n;
        }
        if (!patterns.isEmpty()) {
            filters << QStringLiteral("%1 (%2)").arg(l.name, patterns.join(QLatin1Char(' ')));
        }
    }
    return filters.join(QStringLiteral(";;"));
}

} // namespace neko::pad
