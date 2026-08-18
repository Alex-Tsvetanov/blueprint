#include "testing.hpp"

#include "blueprint/json.hpp"

using bp::Json;

TEST(json, parses_nested_values_of_every_type) {
    const Json j = Json::parse(
        R"({"n":null,"b":true,"i":42,"f":-1.5,"s":"text","a":[1,2,{"deep":"yes"}]})");
    CHECK(j.is_object());
    CHECK(j.at("n").is_null());
    CHECK_EQ(j.at("b").as_bool(), true);
    CHECK_EQ(j.at("i").as_int(), 42LL);
    CHECK(j.at("f").as_number() < -1.4 && j.at("f").as_number() > -1.6);
    CHECK_EQ(j.str("s"), std::string("text"));
    CHECK_EQ(j.at("a").items().size(), std::size_t(3));
    CHECK_EQ(j.at("a").items()[2].str("deep"), std::string("yes"));
}

TEST(json, a_missing_key_reads_as_null_rather_than_throwing) {
    // Clang omits every key whose value it considers the default, so absence
    // is the normal case and must not be an error.
    const Json j = Json::parse(R"({"kind":"FieldDecl"})");
    CHECK(j.at("isImplicit").is_null());
    CHECK_EQ(j.flag("isImplicit"), false);
    CHECK_EQ(j.str("name", "fallback"), std::string("fallback"));
    CHECK(j.find("nothing") == nullptr);
}

TEST(json, writing_then_reading_gives_back_the_same_document) {
    const std::string source =
        R"({"a":[1,2,3],"b":{"c":"x"},"d":true,"e":null,"f":"quote\" and \\ and \n"})";
    const Json first = Json::parse(source);
    const Json second = Json::parse(first.dump());
    CHECK_EQ(first.dump(), second.dump());
    CHECK_EQ(second.at("b").str("c"), std::string("x"));
    CHECK(second.at("f").as_string().find('\n') != std::string::npos);
}

TEST(json, decodes_escapes_including_surrogate_pairs) {
    const Json j = Json::parse(R"({"s":"Aé😀"})");
    const std::string s = j.at("s").as_string();
    CHECK_EQ(s.substr(0, 1), std::string("A"));
    // Two bytes for the accented letter, four for the emoji, one for "A".
    CHECK_EQ(s.size(), std::size_t(7));
}

TEST(json, rejects_malformed_input) {
    CHECK_THROWS(Json::parse("{"));
    CHECK_THROWS(Json::parse("{\"a\":}"));
    CHECK_THROWS(Json::parse("[1,2"));
    CHECK_THROWS(Json::parse("{} trailing"));
    CHECK_THROWS(Json::parse("\"unterminated"));
}

TEST(json, keeps_object_members_in_insertion_order) {
    Json o = Json::object();
    o.set("z", 1);
    o.set("a", 2);
    o.set("m", 3);
    CHECK_EQ(o.dump(), std::string(R"({"z":1,"a":2,"m":3})"));
    o.set("a", 9);  // an existing key is replaced in place, not appended
    CHECK_EQ(o.dump(), std::string(R"({"z":1,"a":9,"m":3})"));
}

TEST(json, pretty_printing_is_reparseable) {
    const Json j = Json::parse(R"({"a":[{"b":1}],"c":[]})");
    const std::string pretty = j.dump(2);
    CHECK(pretty.find('\n') != std::string::npos);
    CHECK_EQ(Json::parse(pretty).dump(), j.dump());
}
