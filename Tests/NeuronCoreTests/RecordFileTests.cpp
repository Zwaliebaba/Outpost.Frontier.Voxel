#include "RecordFile.h"

#include "CppUnitTest.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

TEST_CLASS(RecordFileTests)
{
public:
  TEST_METHOD(AFormattedRecordReadsBackTheSame)
  {
    const Neuron::Record record{"ship", {{"name", "Lancer"}, {"label", "two words"}, {"empty", ""}, {"note", "a # b"}, {"eq", "a=b"}}, 0};
    std::string line;
    Assert::IsTrue(Neuron::FormatRecord(record, line));
    Assert::AreEqual(std::string("ship name=Lancer label=\"two words\" empty=\"\" note=\"a # b\" eq=a=b"), line);
    std::vector<Neuron::Record> read;
    std::string error;
    Assert::IsTrue(Neuron::ParseRecords(line, read, error));
    Assert::AreEqual(std::size_t{1}, read.size());
    Assert::AreEqual(record.fields.size(), read[0].fields.size());
    for (std::size_t i = 0; i < record.fields.size(); ++i)
    {
      Assert::AreEqual(record.fields[i].key, read[0].fields[i].key);
      Assert::AreEqual(record.fields[i].value, read[0].fields[i].value);
    }
  }

  TEST_METHOD(WhatTheFormatCannotHoldIsRefused)
  {
    std::string line;
    Assert::IsFalse(Neuron::FormatRecord(Neuron::Record{"", {}, 0}, line), L"no keyword");
    Assert::IsFalse(Neuron::FormatRecord(Neuron::Record{"a b", {}, 0}, line), L"a space in the keyword");
    Assert::IsFalse(Neuron::FormatRecord(Neuron::Record{"a", {{"k=", "v"}}, 0}, line), L"an equals sign in a key");
    Assert::IsFalse(Neuron::FormatRecord(Neuron::Record{"a", {{"k", "say \"hi\""}}, 0}, line), L"a quote in a value");
    Assert::IsFalse(Neuron::FormatRecord(Neuron::Record{"a", {{"k", "two\nlines"}}, 0}, line), L"a line break in a value");
  }

  TEST_METHOD(SixtyFourBitNumbersParse)
  {
    std::int64_t signedValue = 0;
    Assert::IsTrue(Neuron::ParseInt64("-9000000000123", signedValue));
    Assert::AreEqual(std::int64_t{-9'000'000'000'123}, signedValue);
    std::uint64_t unsignedValue = 0;
    Assert::IsTrue(Neuron::ParseUInt64("18446744073709551615", unsignedValue));
    Assert::IsFalse(Neuron::ParseUInt64("-1", unsignedValue));
    double value = 0.0;
    Assert::IsTrue(Neuron::ParseDouble("3333333.3333333335", value));
    Assert::AreEqual(1.0e7 / 3.0, value);
    Assert::IsFalse(Neuron::ParseDouble("1.5x", value));
  }
};

} // namespace NeuronCoreTests
