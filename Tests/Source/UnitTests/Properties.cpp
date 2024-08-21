/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019-2023 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "../Common/TestsInterface.h"
#include "../Common/TestsShell.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/StyleSheetTypes.h>
#include <doctest.h>

using namespace Rml;

TEST_CASE("Properties")
{
	const Vector2i window_size(1024, 768);

	TestsSystemInterface system_interface;
	TestsRenderInterface render_interface;

	SetRenderInterface(&render_interface);
	SetSystemInterface(&system_interface);

	Rml::Initialise();

	Context* context = Rml::CreateContext("main", window_size);
	ElementDocument* document = context->CreateDocument();

	SUBCASE("flex")
	{
		struct FlexTestCase {
			String flex_value;

			struct ExpectedValues {
				float flex_grow;
				float flex_shrink;
				String flex_basis;
			} expected;
		};

		FlexTestCase tests[] = {
			{"", {0.f, 1.f, "auto"}},
			{"none", {0.f, 0.f, "auto"}},
			{"auto", {1.f, 1.f, "auto"}},
			{"1", {1.f, 1.f, "0px"}},
			{"2", {2.f, 1.f, "0px"}},
			{"2 0", {2.f, 0.f, "0px"}},
			{"2 3", {2.f, 3.f, "0px"}},
			{"2 auto", {2.f, 1.f, "auto"}},
			{"2 0 auto", {2.f, 0.f, "auto"}},
			{"0 0 auto", {0.f, 0.f, "auto"}},
			{"0 0 50px", {0.f, 0.f, "50px"}},
			{"0 0 50px", {0.f, 0.f, "50px"}},
			{"0 0 0", {0.f, 0.f, "0px"}},
		};

		for (const FlexTestCase& test : tests)
		{
			if (!test.flex_value.empty())
			{
				CHECK(document->SetProperty("flex", test.flex_value));
			}

			CHECK(document->GetProperty<float>("flex-grow") == test.expected.flex_grow);
			CHECK(document->GetProperty<float>("flex-shrink") == test.expected.flex_shrink);
			CHECK(document->GetProperty("flex-basis")->ToString() == test.expected.flex_basis);
		}
	}

	SUBCASE("gradient")
	{
		auto ParseGradient = [&](const String& value) -> Property {
			document->SetProperty("decorator", "linear-gradient(" + value + ")");
			auto decorators = document->GetProperty<DecoratorsPtr>("decorator");
			if (!decorators || decorators->list.size() != 1)
				return {};
			for (auto& id_property : decorators->list.front().properties.GetProperties())
			{
				if (id_property.second.unit == Unit::COLORSTOPLIST)
					return id_property.second;
			}
			return {};
		};

		struct GradientTestCase {
			String value;
			String expected_parsed_string;
			ColorStopList expected_color_stops;
		};

		GradientTestCase test_cases[] = {
			{
				"red, blue",
				"#ff0000, #0000ff",
				{
					ColorStop{ColourbPremultiplied(255, 0, 0), NumericValue{}},
					ColorStop{ColourbPremultiplied(0, 0, 255), NumericValue{}},
				},
			},
			{
				"red 5px, blue 50%",
				"#ff0000 5px, #0000ff 50%",
				{
					ColorStop{ColourbPremultiplied(255, 0, 0), NumericValue{5.f, Unit::PX}},
					ColorStop{ColourbPremultiplied(0, 0, 255), NumericValue{50.f, Unit::PERCENT}},
				},
			},
			{
				"red, #00f 50%, rgba(0, 255,0, 150) 10dp",
				"#ff0000, #0000ff 50%, #00ff0096 10dp",
				{
					ColorStop{ColourbPremultiplied(255, 0, 0), NumericValue{}},
					ColorStop{ColourbPremultiplied(0, 0, 255), NumericValue{50.f, Unit::PERCENT}},
					ColorStop{ColourbPremultiplied(0, 150, 0, 150), NumericValue{10.f, Unit::DP}},
				},
			},
			{
				"red 50px 20%, blue 10in",
				"#ff0000 50px, #ff0000 20%, #0000ff 10in",
				{
					ColorStop{ColourbPremultiplied(255, 0, 0), NumericValue{50.f, Unit::PX}},
					ColorStop{ColourbPremultiplied(255, 0, 0), NumericValue{20.f, Unit::PERCENT}},
					ColorStop{ColourbPremultiplied(0, 0, 255), NumericValue{10.f, Unit::INCH}},
				},
			},
		};

		for (const GradientTestCase& test_case : test_cases)
		{
			const Property result = ParseGradient(test_case.value);
			CHECK(result.ToString() == test_case.expected_parsed_string);
			CHECK(result.Get<ColorStopList>() == test_case.expected_color_stops);
		}
	}

	Rml::Shutdown();
}

TEST_CASE("Property.ToString")
{
	TestsSystemInterface system_interface;
	TestsRenderInterface render_interface;
	SetRenderInterface(&render_interface);
	SetSystemInterface(&system_interface);

	Rml::Initialise();

	CHECK(Property(5.2f, Unit::CM).ToString() == "5.2cm");
	CHECK(Property(150, Unit::PERCENT).ToString() == "150%");
	CHECK(Property(Colourb{170, 187, 204, 255}, Unit::COLOUR).ToString() == "#aabbcc");

	auto ParsedValue = [](const String& name, const String& value) -> String {
		PropertyDictionary properties;
		StyleSheetSpecification::ParsePropertyDeclaration(properties, name, value);
		REQUIRE(properties.GetNumProperties() == 1);
		return properties.GetProperties().begin()->second.ToString();
	};

	CHECK(ParsedValue("width", "10px") == "10px");
	CHECK(ParsedValue("width", "10.00em") == "10em");
	CHECK(ParsedValue("width", "auto") == "auto");

	CHECK(ParsedValue("background-color", "#abc") == "#aabbcc");
	CHECK(ParsedValue("background-color", "red") == "#ff0000");

	CHECK(ParsedValue("transform", "translateX(10px)") == "translateX(10px)");
	CHECK(ParsedValue("transform", "translate(20in, 50em)") == "translate(20in, 50em)");

	CHECK(ParsedValue("box-shadow", "2px 2px 0px, rgba(0, 0, 255, 255) 4px 4px 2em") == "#000000 2px 2px 0px, #0000ff 4px 4px 2em");
	CHECK(ParsedValue("box-shadow", "2px 2px 0px, #00ff 4px 4px 2em") == "#000000 2px 2px 0px, #0000ff 4px 4px 2em");

	// Due to conversion to and from premultiplied alpha, some color information is lost.
	CHECK(ParsedValue("box-shadow", "#fff0 2px 2px 0px") == "#00000000 2px 2px 0px");

	CHECK(ParsedValue("decorator", "linear-gradient(110deg, #fff3, #fff 10%, #c33 250dp, #3c3, #33c, #000 90%, #0003) border-box") ==
		"linear-gradient(110deg, #fff3, #fff 10%, #c33 250dp, #3c3, #33c, #000 90%, #0003) border-box");

	CHECK(ParsedValue("filter", "drop-shadow(#000 30px 20px 5px) opacity(0.2) sepia(0.2)") ==
		"drop-shadow(#000 30px 20px 5px) opacity(0.2) sepia(0.2)");

	Rml::Shutdown();
}

static const String basic_rml = R"(
<rml>
<head>
	<style>
	* {
		color: #00ff00;
	}
	body {
		--color-var: #ffffff;
	}
	div {
		background-color: var(--color-var);
		--color2-var: var(--color-var);
	}
	p {
		background-color: var(--color2-var);
		color: var(--missing-var, #ff0000);
	}
	</style>
</head>

<body>
<div id="div"><p id="p"></p></div>
</body>
</rml>
)";

static const String shorthand_rml = R"(
<rml>
<head>
	<style>
	body {
		--padding-var: 20px 5px;
		--v-padding-var: 3px;
		--h-padding-var: 7px;
	}
	div {
		padding: var(--padding-var);
	}
	p {
		padding: var(--v-padding-var) var(--h-padding-var);
	}
	</style>
</head>

<body>
<div id="div"></div>
<p id="p"></p>
</body>
</rml>
)";

static const String datamodel_rml = R"(
<rml>
<head>
	<style>
	div {
		background-color: var(--bg-var, #000000);
	}
	</style>
</head>

<body data-model="vars">
<div id="div" data-var-bg-var="bgcolor"></div>
</body>
</rml>
)";

static const String inheritance_rml = R"(
<rml>
<head>
	<style>
	body {
		--bg-color: #ffffff;
	}
	div {
		--bg-color: #00ff00
	}
	p {
		background-color: var(--bg-color);
	}
	</style>
</head>

<body>
<div><p id="p1"></p></div>
<p id="p2"></p>
</body>
</rml>
)";

static const String media_query_rml = R"(
<rml>
<head>
	<style>
	@media (min-width: 600px) {
		body {
			--bg-color: 255,255,255;
		}
	}
	@media (min-width: 800px) {
		body {
			--bg-color: 0,255,0;
		}
	}
	div {
		background-color: rgba(var(--bg-color), 127);
	}
	</style>
</head>

<body>
<div id="div"></div>
</body>
</rml>
)";

static const String circular_rml = R"(
<rml>
<head>
	<style>
	body {
		--bg-color: var(--bg2-color);
		--bg2-color: var(--bg-color);
	}
	</style>
</head>

<body>
<div></div>
</body>
</rml>
)";

static const String order_rml = R"(
<rml>
<head>
	<style>
	body {
		--bg1-color: var(--bg2-color);
		--bg2-color: var(--bg3-color);
		--bg3-color: "#ffffff";
	}
	</style>
</head>

<body>
<div></div>
</body>
</rml>
)";

static const String transition_rml = R"(
<rml>
<head>
	<style>
		div {
			background-color: red;
			transition: all 0.2s;
		}
		
		div.active {
			--color: blue;
			background-color: var(--color);
		}
	</style>
</head>

<body>
<div></div>
</body>
</rml>
)";

static const String transition_deep_rml = R"(
<rml>
<head>
	<style>
		div {
			background-color: red;
			transition: all 0.2s;
		}
		
		div.active {
			--new-color: blue;
			--color: var(--new-color);
			background-color: var(--color);
		}
	</style>
</head>

<body>
<div></div>
</body>
</rml>
)";

static const String shorthand_transition_rml = R"(
<rml>
<head>
	<style>
		div {
			padding: 10px;
			transition: all 0.2s;
		}
		
		div.active {
			--padding: 20px;
			padding: var(--padding);
		}
	</style>
</head>

<body>
<div></div>
</body>
</rml>
)";

TEST_CASE("variables.basic")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(basic_rml);
	REQUIRE(document);
	document->Show();

	TestsShell::RenderLoop();

	// basic variable
	Element* div = document->GetElementById("div");
	CHECK(div->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(255,255,255,255)");

	// recursive variable
	Element* p = document->GetElementById("p");
	CHECK(p->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(255,255,255,255)");

	// variable fallback
	CHECK(p->GetProperty(PropertyId::Color)->ToString() == "rgba(255,0,0,255)");

	// variable modification
	div->SetProperty("--color-var", "#000000");

	TestsShell::RenderLoop();

	CHECK(div->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(0,0,0,255)");

	// inheritance validation
	CHECK(div->GetProperty("--color-var")->ToString() == "#000000");
	CHECK(document->GetProperty("--color-var")->ToString() == "#ffffff");

	TestsShell::RenderLoop();

	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.shorthands")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(shorthand_rml);
	REQUIRE(document);
	document->Show();

	TestsShell::RenderLoop();

	Element* div = document->GetElementById("div");
	CHECK(div->GetProperty(PropertyId::PaddingTop)->ToString() == "20px");
	CHECK(div->GetProperty(PropertyId::PaddingRight)->ToString() == "5px");

	// variable modification and shorthand override
	div->SetProperty(PropertyId::PaddingTop, Property(6, Unit::PX));

	TestsShell::RenderLoop();

	CHECK(div->GetProperty(PropertyId::PaddingTop)->ToString() == "6px");

	// Change shorthand
	div->SetProperty("--padding-var", "15px 0px");
	div->RemoveProperty(PropertyId::PaddingTop);

	TestsShell::RenderLoop();

	CHECK(div->GetProperty(PropertyId::PaddingTop)->ToString() == "15px");

	// Multi-var shorthand
	Element* p = document->GetElementById("p");
	CHECK(p->GetProperty(PropertyId::PaddingBottom)->ToString() == "3px");
	CHECK(p->GetProperty(PropertyId::PaddingLeft)->ToString() == "7px");

	document->SetProperty("--v-padding-var", "1px");

	TestsShell::RenderLoop();

	CHECK(p->GetProperty(PropertyId::PaddingBottom)->ToString() == "1px");
	CHECK(p->GetProperty(PropertyId::PaddingLeft)->ToString() == "7px");

	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.datamodel")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	auto model = context->CreateDataModel("vars");
	String bgcolor;
	model.Bind("bgcolor", &bgcolor);

	ElementDocument* document = context->LoadDocumentFromMemory(datamodel_rml);
	REQUIRE(document);
	document->Show();

	TestsShell::RenderLoop();

	Element* div = document->GetElementById("div");
	CHECK(div->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(0,0,0,255)");
	bgcolor = "#ffffff";
	model.GetModelHandle().DirtyVariable("bgcolor");

	TestsShell::RenderLoop();

	CHECK(div->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(255,255,255,255)");

	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.inheritance")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(inheritance_rml);
	REQUIRE(document);
	document->Show();

	TestsShell::RenderLoop();

	CHECK(document->GetElementById("p1")->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(0,255,0,255)");
	CHECK(document->GetElementById("p2")->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(255,255,255,255)");

	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.mediaquery")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(media_query_rml);
	REQUIRE(document);
	document->Show();

	TestsShell::RenderLoop();

	context->SetDimensions(Vector2i(800, 320));

	TestsShell::RenderLoop();

	CHECK(document->GetElementById("div")->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(0,255,0,127)");

	context->SetDimensions(Vector2i(600, 320));

	TestsShell::RenderLoop();

	CHECK(document->GetElementById("div")->GetProperty(PropertyId::BackgroundColor)->ToString() == "rgba(255,255,255,127)");

	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.circular")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	// Should get error for resolution failure of the second variable
	TestsShell::SetNumExpectedWarnings(1);

	ElementDocument* document = context->LoadDocumentFromMemory(circular_rml);
	REQUIRE(document);
	document->Show();
	TestsShell::RenderLoop();
	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.order")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	// Should succeed showcasing order-independence of variable definition and usage
	ElementDocument* document = context->LoadDocumentFromMemory(order_rml);
	REQUIRE(document);
	document->Show();
	TestsShell::RenderLoop();
	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.transition")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(transition_rml);
	REQUIRE(document);
	document->Show();
	TestsShell::RenderLoop();

	document->QuerySelector("div")->SetClass("active", true);

	TestsShell::RenderLoop();

	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.transition_deep")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(transition_deep_rml);
	REQUIRE(document);
	document->Show();
	TestsShell::RenderLoop();

	document->QuerySelector("div")->SetClass("active", true);

	TestsShell::RenderLoop();

	document->Close();

	TestsShell::ShutdownShell();
}

TEST_CASE("variables.shorthand_transition")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	ElementDocument* document = context->LoadDocumentFromMemory(shorthand_transition_rml);
	REQUIRE(document);
	document->Show();
	TestsShell::RenderLoop();

	document->QuerySelector("div")->SetClass("active", true);

	TestsShell::RenderLoop();

	document->Close();

	TestsShell::ShutdownShell();
}
