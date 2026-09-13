// ----------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2024 Carlos Carrasco
// ----------------------------------------------------------------------------
#include <gtest/gtest.h>

#include <lightning/http_header.h>


// ----------------------------------------------------------------------------
// test_contains
// ----------------------------------------------------------------------------
TEST (HttpHeader, test_contains) {
  lightning::HttpHeader header;

  ASSERT_FALSE (header.contains ("something"));

  header.set ("TestKey1", "SomeValue1");
  ASSERT_FALSE (header.contains ("something"));
  ASSERT_TRUE (header.contains ("TestKey1"));
  ASSERT_TRUE (header.contains ("testkey1"));

  header.set ("test-key-2", "some-value-2");
  ASSERT_FALSE (header.contains ("something"));
  ASSERT_TRUE (header.contains ("TEST-KEY-2"));
  ASSERT_TRUE (header.contains ("test-key-2"));
}

// ----------------------------------------------------------------------------
// test_set_get
// ----------------------------------------------------------------------------
TEST (HttpHeader, test_set_get) {
  lightning::HttpHeader header;

  header.set ("TestKey1", "SomeValue1");
  ASSERT_FALSE (header.get ("something").has_value());
  ASSERT_TRUE (header.get ("TestKey1").has_value());
  ASSERT_EQ (header.get ("TestKey1").value(), "SomeValue1");
  ASSERT_TRUE (header.get ("testkey1").has_value());
  ASSERT_EQ (header.get ("testkey1").value(), "SomeValue1");

  header.set ("test-key-2", "some-value-2");
  ASSERT_FALSE (header.get ("something"));
  ASSERT_TRUE (header.get ("TEST-KEY-2").has_value());
  ASSERT_EQ (header.get ("TEST-KEY-2").value(), "some-value-2");
  ASSERT_TRUE (header.get ("TEST-KEY-2").has_value());
  ASSERT_EQ (header.get ("test-key-2").value(), "some-value-2");

  header.set ("test-key-2", "new-value");
  ASSERT_EQ (header.get ("test-key-2").value(), "new-value");
}

// ----------------------------------------------------------------------------
// test_size
// ----------------------------------------------------------------------------
TEST (HttpHeader, test_size) {
  lightning::HttpHeader header;

  ASSERT_EQ (header.size(), 0);

  header.set ("aa", "bb");
  ASSERT_EQ (header.size(), 1);

  header.set ("ccc", "ddd");
  ASSERT_EQ (header.size(), 2);
}

// ----------------------------------------------------------------------------
// test_iterator
// ----------------------------------------------------------------------------
TEST (HttpHeader, test_iterator) {
  lightning::HttpHeader header;

  for (auto it = header.begin(); it != header.end(); it++)
    ASSERT_FALSE (true);

  header.set ("AA", "bb");
  for (auto it = header.begin(); it != header.end(); it++) {
    ASSERT_EQ (it->first, "aa");
    ASSERT_EQ (it->second, "bb");
  }

  header.set ("ccc", "ddd");
  size_t count = 0;
  for (auto it = header.cbegin(); it != header.cend(); ++it) {
    if (it->first == "aa")
      EXPECT_EQ (it->second, "bb");
    else {
      EXPECT_EQ (it->first, "ccc");
      EXPECT_EQ (it->second, "ddd");
    }
    ++count;
  }
  EXPECT_EQ (count, 2);
}

TEST (HttpHeader, owns_temporary_values) {
  lightning::HttpHeader header;
  header.set ("x-temporary", std::string (256, 'x'));
  EXPECT_EQ (header.get ("X-TEMPORARY"), std::string (256, 'x'));
  std::string value = "original";
  header.set ("x-mutable", value);
  value.assign ("modified");
  EXPECT_EQ (header.get ("x-mutable"), "original");
}

TEST (HttpHeader, copy_owns_values_and_last_iterator) {
  lightning::HttpHeader copy;
  EXPECT_EQ (copy.last(), copy.end());
  {
    lightning::HttpHeader original;
    original.set ("x-copy", std::string (256, 'x'));
    copy = original;
    copy.last()->second = "changed";
    EXPECT_EQ (original.get ("x-copy"), std::string (256, 'x'));
  }
  EXPECT_EQ (copy.get ("x-copy"), "changed");
  EXPECT_EQ (copy.last()->first, "x-copy");
}
