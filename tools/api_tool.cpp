#include "api_tool.h"

#include <primitives/sw/main.h>
#include <primitives/sw/cl.h>

#include <iostream>
#include <regex>

static bool is_simple(const String &t)
{
    return 0
        || t == "String"
        || t == "Integer"
        || t == "Int"
        || t == "Boolean"
        || t == "Float"
        || t == "Float number"
        || t == "True"
        || t == "False"
        ;
}

static String get_pb_type(String t, bool optional)
{
    //if (!optional)
    {
        if (t == "Integer")
            return "int32";
        if (t == "Boolean")
            return "bool";
        if (t == "Float")
            return "float";
        if (t == "String")
            return "string";
    }
    return t;
}

void Field::save(nlohmann::json &j) const
{
    j["name"] = name;
    for (auto &t : types)
        j["type"].push_back(t);
    if (optional)
        j["optional"] = optional;
    if (array)
        j["array"] = array;
    j["description"] = description;
}

void Field::emitField(primitives::CppEmitter &ctx) const
{
    ctx.addLine("// " + description);
    ctx.addLine();
    emitFieldType(ctx);
    ctx.addText(" " + name + ";");
    ctx.emptyLines();
}

void Field::emitFieldType(primitives::CppEmitter &ctx) const
{
    if (types.empty())
        throw SW_RUNTIME_ERROR("Empty types");

    auto t = types[0];
    auto simple = is_simple(t);
    auto opt = optional && (simple || types.size() != 1);

    if (opt) // we do not need Optional since we have Ptr already
        ctx.addText("Optional<");
    auto a = array;
    while (a--)
        ctx.addText("Vector<");
    if (types.size() > 1)
    {
        ctx.addText("Variant<");
        for (auto &f : types)
            ctx.addText(f + ", ");
        ctx.trimEnd(2);
        ctx.addText(">");
        auto a = array;
        while (a--)
            ctx.addText(">");
    }
    else
    {
        ctx.addText((simple ? "" : "this_namespace::Ptr<") + t + (simple ? "" : ">"));
        auto a = array;
        while (a--)
            ctx.addText(">");
    }
    if (opt)
        ctx.addText(">");
}

void Type::save(nlohmann::json &j) const
{
    j["name"] = name;
    for (auto &f : fields)
    {
        nlohmann::json jf;
        f.save(jf);
        j["fields"].push_back(jf);
    }
    for (auto &f : oneof)
        j["oneof"].push_back(f);
    //if (is_type())
        //j["return_type"] = return_type;
}

void Type::emitType(primitives::CppEmitter &ctx) const
{
    ctx.addLine("// " + description);
    if (!is_oneof())
    {
        ctx.beginBlock("struct " + name);
        ctx.addLine("using Ptr = this_namespace::Ptr<" + name + ">;");
        ctx.emptyLines();
        for (auto &f : fields)
            f.emitField(ctx);
        ctx.endBlock(true);
    }
    else
    {
        ctx.increaseIndent("using " + name + " = Variant<");
        for (auto &f : oneof)
            ctx.addLine(f + ",");
        ctx.trimEnd(1);
        ctx.decreaseIndent(">;");
    }
    ctx.emptyLines();
}

void Type::emitCreateType(primitives::CppEmitter &ctx) const
{
    // from json
    ctx.addLine("template <>");
    ctx.beginFunction("void fromJson(const nlohmann::json &j, " + name + " &v)");
    for (auto &f : fields)
        ctx.addLine("FROM_JSON(" + f.name + ", v." + f.name + ");");
    ctx.endFunction();
    ctx.emptyLines();

    // to json
    ctx.addLine("template <>");
    ctx.beginFunction("nlohmann::json toJson(const " + name + " &r)");
    ctx.addLine("nlohmann::json j;");
    for (auto &f : fields)
        ctx.addLine("TO_JSON(" + f.name + ", r." + f.name + ");");
    ctx.addLine("return j;");
    ctx.endFunction();
    ctx.emptyLines();
}

void Type::emitMethod(const Emitter &e, primitives::CppEmitter &h, primitives::CppEmitter &cpp) const
{
    h.addLine();
    cpp.addLine();

    if (!return_type.types.empty())
    {
        return_type.emitFieldType(h);
        return_type.emitFieldType(cpp);
    }

    auto get_parameters = [this](auto &ctx, bool defaults, int last_non_optional = -1)
    {
        for (const auto &[i,f] : enumerate(fields))
        {
            ctx.addLine("const ");
            f.emitFieldType(ctx);
            ctx.addText(" &");
            ctx.addText(f.name);
            if (f.optional && defaults && i > last_non_optional)
                ctx.addText(" = {}");
            ctx.addText(",");
            if (!f.optional && !defaults)
                last_non_optional = i;
        }
        if (!fields.empty())
            ctx.trimEnd(1);
        return last_non_optional;
    };

    cpp.addText(" Api::" + name + "(");
    cpp.increaseIndent();
    auto lno = get_parameters(cpp, false);
    cpp.decreaseIndent();
    cpp.addLine(") const");

    h.addText(" " + name + "(");
    h.increaseIndent();
    get_parameters(h, true, lno);
    h.decreaseIndent();
    h.addLine(") const;");
    h.emptyLines();

    cpp.beginBlock();
    cpp.addLine("HttpRequestArguments args;");
    cpp.addLine("args.reserve(" + std::to_string(fields.size()) + ");");
    for (auto &f : fields)
        cpp.addLine("TO_REQUEST_ARG(" + f.name + ");");
    cpp.addLine("auto j = SEND_REQUEST(" + name + ");");
    cpp.addLine();
    return_type.emitFieldType(cpp);
    cpp.addText(" r;");
    cpp.addLine("fromJson(j, r);");
    cpp.addLine("return r;");
    cpp.endBlock();
    cpp.emptyLines();
}

void Type::emitFwdDecl(primitives::CppEmitter &ctx) const
{
    if (!is_oneof())
        ctx.addLine("struct " + name + ";");
    else
        emitType(ctx);
}

void Type::emitProtobuf(primitives::CppEmitter &ctx) const
{
    bool is_method = !is_type();

    ctx.increaseIndent("message " + name + (is_method ? "Request" : "") + " {");
    int i = 0;
    for (const auto &f : fields)
    {
        if (f.types.size() > 1)
        {
            ctx.increaseIndent("oneof " + f.name + " {");
            for (auto &t : f.types)
            {
                auto t2 = get_pb_type(t, false);
                ctx.addLine(t2 + " " + f.name + "_" + t2 + " = " + std::to_string(i + 1) + ";");
                i++;
            }
            ctx.decreaseIndent("}");
        }
        else
        {
            auto t = get_pb_type(f.types[0], f.optional);

            auto a = f.array;
            bool ar = a > 1;
            while (a > 1)
            {
                a--;
                auto t2 = t + "_Repeated" + std::to_string(a);
                ctx.increaseIndent("message " + t2 + " {");
                ctx.addLine("repeated " + t + " " + f.name + " = 1;");
                ctx.decreaseIndent("}");
            }

            ctx.addLine();
            if (a > 0 || ar)
            {
                ctx.addText("repeated ");
                if (ar)
                    t = t + "_Repeated1";
            }
            ctx.addText(t + " " + f.name + " = " + std::to_string(i + 1) + ";");
            i++;
        }
    }
    ctx.decreaseIndent("}");
    ctx.emptyLines();
}

Emitter::Emitter(const Parser &p)
{
    for (auto &t : p.types)
        types[t.name] = t;
    for (auto &t : p.methods)
        methods[t.name] = t;
}

String Emitter::emitTypes()
{
    primitives::CppEmitter ctx;
    for (auto &[n, t] : types)
    {
        if (!t.is_oneof())
            t.emitFwdDecl(ctx);
    }
    ctx.emptyLines();
    for (auto &[n, t] : types)
    {
        if (t.is_oneof())
            t.emitFwdDecl(ctx);
    }
    ctx.emptyLines();
    for (auto &[n, t] : types)
    {
        if (!t.is_oneof())
            t.emitType(ctx);
    }
    return ctx.getText();
}

void Emitter::emitMethods() const
{
    primitives::CppEmitter h, cpp;
    for (auto &[n, t] : types)
    {
        cpp.addLine("template <>");
        cpp.addLine("void fromJson<" + t.name + ">(const nlohmann::json &, " + t.name + " &);");
        cpp.addLine("template <>");
        cpp.addLine("nlohmann::json toJson(const " + t.name + " &);");
        cpp.emptyLines();
    }
    cpp.emptyLines();
    for (auto &[n, t] : types)
        t.emitCreateType(cpp);
    cpp.emptyLines();
    for (auto &[n,m] : methods)
        m.emitMethod(*this, h, cpp);
    write_file("methods.inl.h", h.getText());
    write_file("methods.inl.cpp", cpp.getText());
}

String Emitter::emitProtobuf() const
{
    primitives::CppEmitter ctx;
    ctx.addLine("syntax = \"proto3\";");
    ctx.addLine();
    ctx.addLine("package TgBot.api;");
    ctx.addLine();

    // add builtin types
    ctx.addLine(R"(message Integer {
    int32 integer = 1;
}

message Float {
    float float = 1;
}

message Boolean {
    bool boolean = 1;
}

message String {
    string string = 1;
}
)");

    for (auto &[n, t] : types)
        t.emitProtobuf(ctx);
    for (auto &[n, t] : methods)
        t.emitProtobuf(ctx);
    return ctx.getText();
}

int main(int argc, char **argv)
{
    cl::opt<String> target(cl::Positional, cl::Required, cl::desc("Html file or url to parse"));
    cl::opt<path> json("j", cl::desc("Output api as json file"));
    cl::ParseCommandLineOptions(argc, argv);

    std::clog << "Trying to parse " << target << "\n";
    Parser p(read_file_or_download_file(target));

    p.enumerateSectionChildren("getting-updates");
    p.enumerateSectionChildren("available-types");
    p.enumerateSectionChildren("available-methods");
    p.enumerateSectionChildren("updating-messages");
    p.enumerateSectionChildren("stickers");
    p.enumerateSectionChildren("inline-mode");
    p.enumerateSectionChildren("payments");
    p.enumerateSectionChildren("telegram-passport");
    p.enumerateSectionChildren("games");

    // json
    {
        nlohmann::json j;
        for (auto &t : p.types)
            t.save(j["types"][t.name]);
        for (auto &t : p.methods)
            t.save(j["methods"][t.name]);
        if (!json.empty())
            write_file(json, j.dump(2));
        //else
        //std::cout << j.dump(2) << "\n";
    }

    Emitter e(p);

    write_file("types.inl.h", e.emitTypes());
    e.emitMethods();

    // pb impl won't work because of vector<vector<>> types :(
    write_file("tgapi.proto", e.emitProtobuf());

    return 0;
}
