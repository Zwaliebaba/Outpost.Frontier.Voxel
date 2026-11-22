#include "SaveFile.h"

#include "FileSys.h"

#include "CppUnitTest.h"

#include <filesystem>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

[[nodiscard]] std::vector<Neuron::SaveSection> TwoSections()
{
  Neuron::SaveWriter writer;
  writer.BeginSection("Alpha");
  writer.Add("thing").Field("name", "Lancer").Field("label", "two words");
  writer.Add("empty").Field("value", "");
  writer.BeginSection("Beta");
  writer.Add("count").Field("n", "3");
  return writer.Sections();
}

[[nodiscard]] std::string Formatted(const std::vector<Neuron::SaveSection>& _sections)
{
  std::string text;
  std::string error;
  Assert::IsTrue(Neuron::FormatSave(_sections, "a test save", text, error), Wide(error).c_str());
  return text;
}

[[nodiscard]] std::string Refusal(std::string_view _text)
{
  std::vector<Neuron::SaveSection> sections;
  std::string error;
  Assert::IsFalse(Neuron::ParseSave(_text, sections, error));
  Assert::IsFalse(error.empty());
  return error;
}

} // namespace

TEST_CLASS(SaveFileTests)
{
public:
  TEST_METHOD(SectionsRoundTrip)
  {
    const std::string text = Formatted(TwoSections());
    std::vector<Neuron::SaveSection> read;
    std::string error;
    Assert::IsTrue(Neuron::ParseSave(text, read, error), Wide(error).c_str());
    Assert::AreEqual(std::size_t{2}, read.size());
    Assert::AreEqual(std::string("Alpha"), read[0].name);
    Assert::AreEqual(std::size_t{2}, read[0].records.size());
    Assert::AreEqual(std::string("two words"), *read[0].records[0].Find("label"));
    Assert::AreEqual(std::string(""), *read[0].records[1].Find("value"));
    Assert::AreEqual(std::string("3"), *read[1].records[0].Find("n"));
  }

  TEST_METHOD(TheTextIsReadableRecords)
  {
    const std::string text = Formatted(TwoSections());
    Assert::AreEqual(std::size_t{0}, text.find("# a test save\r\nsave version=1\r\nsection name=Alpha\r\n"));
    Assert::AreNotEqual(std::string::npos, text.find("thing name=Lancer label=\"two words\"\r\n"));
    Assert::AreNotEqual(std::string::npos, text.find("end sections=2 records=3\r\n"));
  }

  TEST_METHOD(AFileCutShortIsRefused)
  {
    const std::string text = Formatted(TwoSections());
    const std::size_t end = text.find("end ");
    Assert::IsTrue(Refusal(text.substr(0, end)).find("ends before its end record") != std::string::npos);
    // Cut inside a section: the end record is gone too.
    Assert::IsTrue(Refusal(text.substr(0, text.find("section name=Beta"))).find("incomplete") != std::string::npos);
  }

  TEST_METHOD(AnEndRecordThatDoesNotCountTheFileIsRefused)
  {
    std::string text = Formatted(TwoSections());
    const std::size_t count = text.find("count n=3\r\n");
    text.erase(count, std::string("count n=3\r\n").size());
    Assert::IsTrue(Refusal(text).find("counts 2 section(s) and 3 record(s)") != std::string::npos);
  }

  TEST_METHOD(OtherFilesAreRefused)
  {
    Assert::IsTrue(Refusal("model name=Box\r\n").find("not a save file") != std::string::npos);
    Assert::IsTrue(Refusal("save version=2\r\nend sections=0 records=0\r\n").find("version 2") != std::string::npos);
    Assert::IsTrue(Refusal("save version=1\r\nthing a=1\r\nend sections=0 records=1\r\n").find("before the first section") !=
                   std::string::npos);
    Assert::IsTrue(Refusal("save version=1\r\nsection name=A\r\nsection name=A\r\nend sections=2 records=0\r\n").find("twice") !=
                   std::string::npos);
    Assert::IsTrue(Refusal("save version=1\r\nend sections=0 records=0\r\nsection name=A\r\n").find("after the end record") !=
                   std::string::npos);
  }

  TEST_METHOD(WhatTheFormatCannotHoldIsRefusedWhenWritten)
  {
    std::string text;
    std::string error;
    Neuron::SaveWriter quote;
    quote.BeginSection("A");
    quote.Add("thing").Field("name", "say \"hi\"");
    Assert::IsFalse(Neuron::FormatSave(quote.Sections(), "", text, error));

    Neuron::SaveWriter twice;
    twice.BeginSection("A");
    twice.BeginSection("A");
    Assert::IsFalse(Neuron::FormatSave(twice.Sections(), "", text, error));

    Neuron::SaveWriter reserved;
    reserved.BeginSection("A");
    reserved.Add("end").Field("sections", "0");
    Assert::IsFalse(Neuron::FormatSave(reserved.Sections(), "", text, error));

    Neuron::SaveWriter loose;
    loose.Add("thing").Field("a", "1");
    Assert::IsFalse(Neuron::FormatSave(loose.Sections(), "", text, error));
  }

  TEST_METHOD(WritingReplacesTheFileWhole)
  {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / L"OutpostSaveFileTests";
    std::filesystem::remove_all(directory);
    const std::filesystem::path path = directory / L"Save.txt";
    std::string error;
    Assert::IsTrue(Neuron::WriteSaveFile(path, "first", error), Wide(error).c_str());
    Assert::IsTrue(Neuron::WriteSaveFile(path, "second", error), Wide(error).c_str());
    std::string text;
    Assert::IsTrue(Neuron::ReadTextFile(path, text));
    Assert::AreEqual(std::string("second"), text);
    std::filesystem::path partial = path;
    partial += L".partial";
    Assert::IsFalse(std::filesystem::exists(partial), L"nothing is left beside the save");
    std::filesystem::remove_all(directory);
  }
};

} // namespace NeuronServerTests
