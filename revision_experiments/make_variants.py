"""Generate the ablation and anchor-stress variants of compressor.cpp.

Usage: python3 make_variants.py ../compressor.cpp OUT_DIR

Writes OUT_DIR/compressor_setting1.cpp, compressor_setting2.cpp, and compressor_stress.cpp.
All variants stay lossless, so their archives can be checked with SHA-256 like the default build.

- Setting 1 (no intrinsic structure, no extrinsic context): every variable token gets the generic
  signature <*>, is stored verbatim, and the <*> stream is always dictionary-encoded.
- Setting 2 (no extrinsic context): signatures keep the intrinsic structure but drop CTX.
- Stress: alphabetic tokens selected by DELOG_STRESS_AFTER (the token right after this keyword)
  or DELOG_STRESS_TOKENS (comma-separated tokens) are treated as variables instead of context
  anchors. They are stored verbatim under the signature <CTX=ctx|STR=_>.
"""
import os
import sys


def sub_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f"pattern found {text.count(old)} times: {old[:80]!r}")
    return text.replace(old, new)


def function_span(text, signature):
    start = text.index(signature)
    end = text.index("\n}\n", start) + 3
    return start, end


def main(src_path, out_dir):
    src = open(src_path).read()
    os.makedirs(out_dir, exist_ok=True)

    variable_block = """    if (is_variable) {
        std::string compact_id = tag_manager.get_or_create_id(full_tag);"""
    numeric_check = """            bool all_values_are_numeric = std::all_of(values.begin(), values.end(), [](const auto& s){ return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit); });"""

    # Setting 1
    s1 = sub_once(src, variable_block, """    if (is_variable) {
        full_tag = "<*>";
        value_to_store = std::string(token);
        std::string compact_id = tag_manager.get_or_create_id(full_tag);""")
    s1 = sub_once(s1, numeric_check, """            if (tag_name == "<*>") { dictionary_encode_and_store(); continue; }
""" + numeric_check)
    open(os.path.join(out_dir, "compressor_setting1.cpp"), "w").write(s1)

    # Setting 2
    a, b = function_span(src, "void process_sub_token_single_pass(")
    body = src[a:b].replace("build_structured_tag(context,", 'build_structured_tag("",')
    open(os.path.join(out_dir, "compressor_setting2.cpp"), "w").write(src[:a] + body + src[b:])

    # Stress variant
    stress = sub_once(src, """    } else {
        result_line.append(token);
        context = token;
    }
}""", """    } else {
        static const std::string stress_after = [] { const char* v = std::getenv("DELOG_STRESS_AFTER"); return v ? std::string(v) : std::string(); }();
        static const std::unordered_set<std::string> stress_tokens = [] {
            std::unordered_set<std::string> out; const char* v = std::getenv("DELOG_STRESS_TOKENS");
            std::string s = v ? v : ""; size_t p = 0;
            while (p < s.size()) { size_t q = s.find(',', p); if (q == std::string::npos) q = s.size(); if (q > p) out.insert(s.substr(p, q - p)); p = q + 1; }
            return out; }();
        bool as_variable = (!stress_after.empty() && context == stress_after) || stress_tokens.count(std::string(token)) > 0;
        if (as_variable) {
            std::string tag = build_structured_tag(context, "_", std::nullopt, std::nullopt);
            std::string compact_id = tag_manager.get_or_create_id(tag);
            result_line.append("<").append(compact_id).append(">");
            local_tag_data[tag].push_back(std::string(token));
        } else {
            result_line.append(token);
            context = token;
        }
    }
}""")
    if "#include <cstdlib>" not in stress:
        stress = "#include <cstdlib>\n" + stress
    open(os.path.join(out_dir, "compressor_stress.cpp"), "w").write(stress)
    print("variants written to", out_dir)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
