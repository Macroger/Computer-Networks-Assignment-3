#define CATCH_CONFIG_MAIN
#include "catch2/catch.hpp"
#include "../server.cpp"  // Include server implementation

// ============================================================================
// TEST SUITE: split_fields_until
// ============================================================================

TEST_CASE("split_fields_until - basic splitting", "[split_fields_until]") {
    std::string text = "cmd}+{author}+{title}+{message";
    std::string delim = "}+{";
    size_t endPos = text.size();
    
    auto fields = split_fields_until(text, delim, endPos);
    
    REQUIRE(fields.size() == 4);
    REQUIRE(fields[0] == "cmd");
    REQUIRE(fields[1] == "author");
    REQUIRE(fields[2] == "title");
    REQUIRE(fields[3] == "message");
}

TEST_CASE("split_fields_until - stops at endPos", "[split_fields_until]") {
    std::string text = "cmd}+{author}+{title}+{message}+{extra";
    std::string delim = "}+{";
    // Stop just before the }+{ that precedes "extra"
    size_t extraDelimPos = text.find("}+{extra");
    size_t endPos = extraDelimPos;  // Stop at the delimiter itself, not including it
    
    auto fields = split_fields_until(text, delim, endPos);
    
    REQUIRE(fields.size() == 4);  // cmd, author, title, message
    REQUIRE(fields[0] == "cmd");
    REQUIRE(fields[1] == "author");
    REQUIRE(fields[2] == "title");
    REQUIRE(fields[3] == "message");
}

TEST_CASE("split_fields_until - preserves empty fields", "[split_fields_until]") {
    std::string text = "cmd}+{}+{title}+{";  // Empty author, message
    std::string delim = "}+{";
    size_t endPos = text.size();
    
    auto fields = split_fields_until(text, delim, endPos);
    
    REQUIRE(fields.size() == 4);
    REQUIRE(fields[0] == "cmd");
    REQUIRE(fields[1] == "");  // Empty author
    REQUIRE(fields[2] == "title");
    REQUIRE(fields[3] == "");  // Empty message
}

TEST_CASE("split_fields_until - single field", "[split_fields_until]") {
    std::string text = "onlycommand";
    std::string delim = "}+{";
    size_t endPos = text.size();
    
    auto fields = split_fields_until(text, delim, endPos);
    
    REQUIRE(fields.size() == 1);
    REQUIRE(fields[0] == "onlycommand");
}

// ============================================================================
// TEST SUITE: parse_message
// ============================================================================

TEST_CASE("parse_message - GET_BOARD with no filters", "[parse_message]") {
    std::string msg = "GET_BOARD}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == true);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::GET_BOARD);
    REQUIRE(result.filter_author == "");
    REQUIRE(result.filter_title == "");
}

TEST_CASE("parse_message - GET_BOARD with author filter", "[parse_message]") {
    std::string msg = "GET_BOARD}+{Alice}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == true);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::GET_BOARD);
    REQUIRE(result.filter_author == "Alice");
    REQUIRE(result.filter_title == "");
}

TEST_CASE("parse_message - GET_BOARD with both filters", "[parse_message]") {
    std::string msg = "GET_BOARD}+{Bob}+{Tutorial}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == true);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::GET_BOARD);
    REQUIRE(result.filter_author == "Bob");
    REQUIRE(result.filter_title == "Tutorial");
}

TEST_CASE("parse_message - POST with single post", "[parse_message]") {
    std::string msg = "POST}+{Alice}+{Hello}+{This is a message}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == true);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::POST);
    REQUIRE(result.posts.size() == 1);
    REQUIRE(result.posts[0].author == "Alice");
    REQUIRE(result.posts[0].title == "Hello");
    REQUIRE(result.posts[0].message == "This is a message");
}

TEST_CASE("parse_message - POST with multiple posts", "[parse_message]") {
    std::string msg = "POST}+{Alice}+{Title1}+{Message1}+{Bob}+{Title2}+{Message2}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == true);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::POST);
    REQUIRE(result.posts.size() == 2);
    
    REQUIRE(result.posts[0].author == "Alice");
    REQUIRE(result.posts[0].title == "Title1");
    REQUIRE(result.posts[0].message == "Message1");
    
    REQUIRE(result.posts[1].author == "Bob");
    REQUIRE(result.posts[1].title == "Title2");
    REQUIRE(result.posts[1].message == "Message2");
}

TEST_CASE("parse_message - POST anonymous (empty author/title)", "[parse_message]") {
    std::string msg = "POST}+{}+{}+{Anonymous message}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == true);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::POST);
    REQUIRE(result.posts.size() == 1);
    REQUIRE(result.posts[0].author == "");
    REQUIRE(result.posts[0].title == "");
    REQUIRE(result.posts[0].message == "Anonymous message");
}

TEST_CASE("parse_message - POST error: empty message", "[parse_message]") {
    std::string msg = "POST}+{Alice}+{Title}+{}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == false);
    REQUIRE(result.error.find("message cannot be empty") != std::string::npos);
}

TEST_CASE("parse_message - POST error: incomplete triple", "[parse_message]") {
    std::string msg = "POST}+{Alice}+{Title}}&{{";  // Missing message
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == false);
    REQUIRE(result.error.find("requires triples") != std::string::npos);
}

TEST_CASE("parse_message - POST error: no posts", "[parse_message]") {
    std::string msg = "POST}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == false);
    REQUIRE(result.error.find("no (Author, Title, Message)") != std::string::npos);
}

TEST_CASE("parse_message - QUIT command", "[parse_message]") {
    std::string msg = "QUIT}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == true);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::QUIT);
}

TEST_CASE("parse_message - invalid command", "[parse_message]") {
    std::string msg = "BADCMD}}&{{";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == false);
    REQUIRE(result.clientCmd == CLIENT_COMMANDS::INVALID_COMMAND);
    REQUIRE(result.error.find("Invalid command") != std::string::npos);
}

TEST_CASE("parse_message - empty message", "[parse_message]") {
    std::string msg = "";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    REQUIRE(result.ok == false);
    REQUIRE(result.error.find("Empty message") != std::string::npos);
}

TEST_CASE("parse_message - message with separator (single chunk)", "[parse_message]") {
    std::string msg = "POST}+{Alice}+{Title}+{Msg1}#{POST}+{Bob}+{Title2}+{Msg2}";
    
    auto result = parse_message(msg, "}+{", "}#{", "}}&{{");
    
    // Should parse only up to the separator
    REQUIRE(result.ok == true);
    REQUIRE(result.posts.size() == 1);  // Only first post
    REQUIRE(result.posts[0].author == "Alice");
}

// ============================================================================
// TEST SUITE: build_post_error
// ============================================================================

TEST_CASE("build_post_error - formats error correctly", "[build_post_error]") {
    std::string error = "Invalid author";
    std::string response = build_post_error(error);
    
    REQUIRE(response.find("POST_ERROR") != std::string::npos);
    REQUIRE(response.find(error) != std::string::npos);
    REQUIRE(response.find("}}&{{") != std::string::npos);  // Has terminator
}

TEST_CASE("build_post_error - includes delimiters", "[build_post_error]") {
    std::string error = "Test error";
    std::string response = build_post_error(error);
    
    // Should have format: POST_ERROR}+{}+{}+{error}}&{{
    REQUIRE(response.find("}+{") != std::string::npos);
}

// ============================================================================
// TEST SUITE: build_post_ok
// ============================================================================

TEST_CASE("build_post_ok - formats success correctly", "[build_post_ok]") {
    std::string response = build_post_ok();
    
    REQUIRE(response.find("POST_OK") != std::string::npos);
    REQUIRE(response.find("}}&{{") != std::string::npos);  // Has terminator
}

TEST_CASE("build_post_ok - includes delimiters and empty fields", "[build_post_ok]") {
    std::string response = build_post_ok();
    
    // Should have format: POST_OK}+{}+{}+{}}&{{
    REQUIRE(response.find("}+{") != std::string::npos);
}