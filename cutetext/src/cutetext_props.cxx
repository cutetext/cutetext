// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cutetext_props.cxx
 * @author James Zeng
 * @date 2018-08-12
 * @brief CuteText Properties management.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <clocale>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <memory>

#include <fcntl.h>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "GUI.h"
#include "ScintillaWindow.h"

#if defined(__unix__)

const GUI::gui_char menuAccessIndicator[] = GUI_TEXT("_");

#else

const GUI::gui_char menuAccessIndicator[] = GUI_TEXT("&");

#endif

#include "StringList.h"
#include "StringHelpers.h"
#include "FilePath.h"
#include "StyleDefinition.h"
#include "PropSetFile.h"
#include "StyleWriter.h"
#include "Extender.h"
#include "SciTE.h"
#include "Mutex.h"
#include "JobQueue.h"
#include "Cookie.h"
#include "Worker.h"
#include "MatchMarker.h"
#include "EditorConfig.h"
#include "SciTEBase.h"
#include "IFaceTable.h"

void SciTEBase::SetImportMenu() {
	for (int i = 0; i < kImportMax; i++) {
		DestroyMenuItem(kMenuOptions, kImportCmdID + i);
	}
	if (!importFiles_.empty()) {
		for (int stackPos = 0; stackPos < static_cast<int>(importFiles_.size()) && stackPos < kImportMax; stackPos++) {
			const int itemID = kImportCmdID + stackPos;
			if (importFiles_[stackPos].IsSet()) {
				GUI::gui_string entry = localiser_.Text("Open");
				entry += GUI_TEXT(" ");
				entry += importFiles_[stackPos].Name().AsInternal();
				SetMenuItem(kMenuOptions, IMPORT_START + stackPos, itemID, entry.c_str());
			}
		}
	}
}

void SciTEBase::ImportMenu(int pos) {
	if (pos >= 0) {
		if (importFiles_[pos].IsSet()) {
			Open(importFiles_[pos]);
		}
	}
}

void SciTEBase::SetLanguageMenu() {
	for (int i = 0; i < 100; i++) {
		DestroyMenuItem(kMenuLanguage, kLanguageCmdID + i);
	}
	for (unsigned int item = 0; item < languageMenu_.size(); item++) {
		const int itemID = kLanguageCmdID + item;
		GUI::gui_string entry = localiser_.Text(languageMenu_[item].menuItem.c_str());
		if (languageMenu_[item].menuKey.length()) {
#if defined(GTK)
			entry += GUI_TEXT(" ");
#else
			entry += GUI_TEXT("\t");
#endif
			entry += GUI::StringFromUTF8(languageMenu_[item].menuKey);
		}
		if (entry.size() && entry[0] != '#') {
			SetMenuItem(kMenuLanguage, item, itemID, entry.c_str());
		}
	}
}

// Null except on Windows where it may be overridden
void SciTEBase::ReadEmbeddedProperties() {
}

const GUI::gui_char propLocalFileName[] = GUI_TEXT("SciTE.properties");
const GUI::gui_char propDirectoryFileName[] = GUI_TEXT("SciTEDirectory.properties");

void SciTEBase::ReadEnvironment() {
#if defined(__unix__)
	extern char **environ;
	char **e = environ;
#else
	char **e = _environ;
#endif
	for (; e && *e; e++) {
		char key[1024];
		char *k = *e;
		char *v = strchr(k, '=');
		if (v && (static_cast<size_t>(v - k) < sizeof(key))) {
			memcpy(key, k, v - k);
			key[v - k] = '\0';
			propsPlatform_.Set(key, v + 1);
		}
	}
}

/**
Read global and user properties files.
*/
void SciTEBase::ReadGlobalPropFile() {
	std::string excludes;
	std::string includes;

	// Want to apply imports.exclude and imports.include but these may well be in
	// user properties.

	for (int attempt=0; attempt<2; attempt++) {

		std::string excludesRead = props_.GetString("imports.exclude");
		std::string includesRead = props_.GetString("imports.include");
		if ((attempt > 0) && ((excludesRead == excludes) && (includesRead == includes)))
			break;

		excludes = excludesRead;
		includes = includesRead;

		filter_.SetFilter(excludes.c_str(), includes.c_str());

		importFiles_.clear();

		ReadEmbeddedProperties();

		propsBase_.Clear();
		FilePath propfileBase = GetDefaultPropertiesFileName();
		propsBase_.Read(propfileBase, propfileBase.Directory(), filter_, &importFiles_, 0);

		propsUser_.Clear();
		FilePath propfileUser = GetUserPropertiesFileName();
		propsUser_.Read(propfileUser, propfileUser.Directory(), filter_, &importFiles_, 0);
	}

	if (!localiser_.read) {
		ReadLocalization();
	}
}

void SciTEBase::ReadAbbrevPropFile() {
	propsAbbrev_.Clear();
	propsAbbrev_.Read(pathAbbreviations_, pathAbbreviations_.Directory(), filter_, &importFiles_, 0);
}

/**
Reads the directory properties file depending on the variable
"properties.directory.enable". Also sets the variable $(SciteDirectoryHome) to the path
where this property file is found. If it is not found $(SciteDirectoryHome) will
be set to $(FilePath).
*/
void SciTEBase::ReadDirectoryPropFile() {
	propsDirectory_.Clear();

	if (props_.GetInt("properties.directory.enable") != 0) {
		FilePath propfile = GetDirectoryPropertiesFileName();
		props_.Set("SciteDirectoryHome", propfile.Directory().AsUTF8().c_str());

		propsDirectory_.Read(propfile, propfile.Directory(), filter_, NULL, 0);
	}
}

/**
Read local and directory properties file.
*/
void SciTEBase::ReadLocalPropFile() {
	// The directory properties acts like a base local properties file.
	// Therefore it must be read always before reading the local prop file.
	ReadDirectoryPropFile();

	FilePath propfile = GetLocalPropertiesFileName();

	propsLocal_.Clear();
	propsLocal_.Read(propfile, propfile.Directory(), filter_, NULL, 0);

	props_.Set("Chrome", "#C0C0C0");
	props_.Set("ChromeHighlight", "#FFFFFF");

	FilePath fileDirectory = filePath_.Directory();
	editorConfig_->Clear();
	if (props_.GetInt("editor.config.enable", 0)) {
		editorConfig_->ReadFromDirectory(fileDirectory);
	}
}

Colour ColourOfProperty(const PropSetFile &props_, const char *key, Colour colourDefault) {
	std::string colour = props_.GetExpandedString(key);
	if (colour.length()) {
		return ColourFromString(colour);
	}
	return colourDefault;
}

/**
 * Put the next property item from the given property string
 * into the buffer pointed by @a pPropItem.
 * @return NULL if the end of the list is met, else, it points to the next item.
 */
const char *SciTEBase::GetNextPropItem(
	const char *pStart,	/**< the property string to parse for the first call,
						 * pointer returned by the previous call for the following. */
	char *pPropItem,	///< pointer on a buffer receiving the requested prop item
	int maxLen)			///< size of the above buffer
{
	ptrdiff_t size = maxLen - 1;

	*pPropItem = '\0';
	if (pStart == NULL) {
		return NULL;
	}
	const char *pNext = strchr(pStart, ',');
	if (pNext) {	// Separator is found
		if (size > pNext - pStart) {
			// Found string fits in buffer
			size = pNext - pStart;
		}
		pNext++;
	}
	strncpy(pPropItem, pStart, size);
	pPropItem[size] = '\0';
	return pNext;
}

std::string SciTEBase::StyleString(const char *lang, int style) const {
	char key[200];
	sprintf(key, "style.%s.%0d", lang, style);
	return props_.GetExpandedString(key);
}

StyleDefinition SciTEBase::StyleDefinitionFor(int style) {
	const std::string languageName = !StartsWith(language_, "lpeg_") ? language_ : "lpeg";

	const std::string ssDefault = StyleString("*", style);
	std::string ss = StyleString(languageName.c_str(), style);

	if (!subStyleBases_.empty()) {
		const int baseStyle = wEditor_.Call(SCI_GETSTYLEFROMSUBSTYLE, style);
		if (baseStyle != style) {
			const int primaryStyle = wEditor_.Call(SCI_GETPRIMARYSTYLEFROMSTYLE, style);
			const int distanceSecondary = (style == primaryStyle) ? 0 : wEditor_.Call(SCI_DISTANCETOSECONDARYSTYLES);
			const int primaryBase = baseStyle - distanceSecondary;
			const int subStylesStart = wEditor_.Call(SCI_GETSUBSTYLESSTART, primaryBase);
			const int subStylesLength = wEditor_.Call(SCI_GETSUBSTYLESLENGTH, primaryBase);
			const int subStyle = style - (subStylesStart + distanceSecondary);
			if (subStyle < subStylesLength) {
				char key[200];
				sprintf(key, "style.%s.%0d.%0d", languageName.c_str(), baseStyle, subStyle + 1);
				ss = props_.GetNewExpandString(key);
			}
		}
	}

	StyleDefinition sd(ssDefault);
	sd.ParseStyleDefinition(ss);
	return sd;
}

void SciTEBase::SetOneStyle(GUI::ScintillaWindow &win, int style, const StyleDefinition &sd) {
	if (sd.specified & StyleDefinition::sdItalics)
		win.Call(SCI_STYLESETITALIC, style, sd.italics ? 1 : 0);
	if (sd.specified & StyleDefinition::sdWeight)
		win.Call(SCI_STYLESETWEIGHT, style, sd.weight);
	if (sd.specified & StyleDefinition::sdFont)
		win.CallString(SCI_STYLESETFONT, style,
			sd.font.c_str());
	if (sd.specified & StyleDefinition::sdFore)
		win.Call(SCI_STYLESETFORE, style, sd.ForeAsLong());
	if (sd.specified & StyleDefinition::sdBack)
		win.Call(SCI_STYLESETBACK, style, sd.BackAsLong());
	if (sd.specified & StyleDefinition::sdSize)
		win.Call(SCI_STYLESETSIZEFRACTIONAL, style, sd.FractionalSize());
	if (sd.specified & StyleDefinition::sdEOLFilled)
		win.Call(SCI_STYLESETEOLFILLED, style, sd.eolfilled ? 1 : 0);
	if (sd.specified & StyleDefinition::sdUnderlined)
		win.Call(SCI_STYLESETUNDERLINE, style, sd.underlined ? 1 : 0);
	if (sd.specified & StyleDefinition::sdCaseForce)
		win.Call(SCI_STYLESETCASE, style, sd.caseForce);
	if (sd.specified & StyleDefinition::sdVisible)
		win.Call(SCI_STYLESETVISIBLE, style, sd.visible ? 1 : 0);
	if (sd.specified & StyleDefinition::sdChangeable)
		win.Call(SCI_STYLESETCHANGEABLE, style, sd.changeable ? 1 : 0);
	win.Call(SCI_STYLESETCHARACTERSET, style, characterSet_);
}

void SciTEBase::SetStyleBlock(GUI::ScintillaWindow &win, const char *lang, int start, int last) {
	for (int style = start; style <= last; style++) {
		if (style != STYLE_DEFAULT) {
			char key[200];
			sprintf(key, "style.%s.%0d", lang, style-start);
			std::string sval = props_.GetExpandedString(key);
			if (sval.length()) {
				SetOneStyle(win, style, StyleDefinition(sval));
			}
		}
	}
}

void SciTEBase::SetStyleFor(GUI::ScintillaWindow &win, const char *lang) {
	SetStyleBlock(win, lang, 0, STYLE_MAX);
}

void SciTEBase::SetOneIndicator(GUI::ScintillaWindow &win, int indicator, const IndicatorDefinition &ind) {
	win.Call(SCI_INDICSETSTYLE, indicator, ind.style);
	win.Call(SCI_INDICSETFORE, indicator, ind.colour);
	win.Call(SCI_INDICSETALPHA, indicator, ind.fillAlpha);
	win.Call(SCI_INDICSETOUTLINEALPHA, indicator, ind.outlineAlpha);
	win.Call(SCI_INDICSETUNDER, indicator, ind.under);
}

std::string SciTEBase::ExtensionFileName() const {
	if (CurrentBufferConst()->overrideExtension.length()) {
		return CurrentBufferConst()->overrideExtension;
	} else {
		FilePath name = FileNameExt();
		if (name.IsSet()) {
#if !defined(GTK)
			// Force extension to lower case
			std::string extension = name.Extension().AsUTF8();
			if (extension.empty()) {
				return name.AsUTF8();
			} else {
				LowerCaseAZ(extension);
				return name.BaseName().AsUTF8() + "." + extension;
			}
#else
			return name.AsUTF8();
#endif
		} else {
			return props_.GetString("default.file.ext");
		}
	}
}

void SciTEBase::ForwardPropertyToEditor(const char *key) {
	if (props_.Exists(key)) {
		std::string value = props_.GetExpandedString(key);
		wEditor_.CallString(SCI_SETPROPERTY,
						 UptrFromString(key), value.c_str());
		wOutput_.CallString(SCI_SETPROPERTY,
						 UptrFromString(key), value.c_str());
	}
}

void SciTEBase::DefineMarker(int marker, int markerType, Colour fore, Colour back, Colour backSelected) {
	wEditor_.Call(SCI_MARKERDEFINE, marker, markerType);
	wEditor_.Call(SCI_MARKERSETFORE, marker, fore);
	wEditor_.Call(SCI_MARKERSETBACK, marker, back);
	wEditor_.Call(SCI_MARKERSETBACKSELECTED, marker, backSelected);
}

void SciTEBase::ReadAPI(const std::string &fileNameForExtension) {
	std::string sApiFileNames = props_.GetNewExpandString("api.",
	                        fileNameForExtension.c_str());
	if (sApiFileNames.length() > 0) {
		std::vector<std::string> vApiFileNames = StringSplit(sApiFileNames, ';');
		std::vector<char> data;

		// Load files into data
		for (const std::string &vApiFileName : vApiFileNames) {
			std::string contents = FilePath(GUI::StringFromUTF8(vApiFileName)).Read();
			data.insert(data.end(), contents.begin(), contents.end());
		}

		// Initialise apis
		if (data.size() > 0) {
			apis_.Set(data);
		}
	}
}

std::string SciTEBase::FindLanguageProperty(const char *pattern, const char *defaultValue) {
	std::string key = pattern;
	Substitute(key, "*", language_.c_str());
	std::string ret = props_.GetExpandedString(key.c_str());
	if (ret == "")
		ret = props_.GetExpandedString(pattern);
	if (ret == "")
		ret = defaultValue;
	return ret;
}

/**
 * A list of all the properties that should be forwarded to Scintilla lexers.
 */
static const char *propertiesToForward[] = {
	"fold.lpeg.by.indentation",
	"lexer.lpeg.color.theme",
	"lexer.lpeg.home",
	"lexer.lpeg.script",
//++Autogenerated -- run ../scripts/RegenerateSource.py to regenerate
//**\(\t"\*",\n\)
	"asp.default.language",
	"fold",
	"fold.abl.comment.multiline",
	"fold.abl.syntax.based",
	"fold.asm.comment.explicit",
	"fold.asm.comment.multiline",
	"fold.asm.explicit.anywhere",
	"fold.asm.explicit.end",
	"fold.asm.explicit.start",
	"fold.asm.syntax.based",
	"fold.at.else",
	"fold.baan.inner.level",
	"fold.baan.keywords.based",
	"fold.baan.sections",
	"fold.baan.syntax.based",
	"fold.basic.comment.explicit",
	"fold.basic.explicit.anywhere",
	"fold.basic.explicit.end",
	"fold.basic.explicit.start",
	"fold.basic.syntax.based",
	"fold.coffeescript.comment",
	"fold.comment",
	"fold.comment.nimrod",
	"fold.comment.yaml",
	"fold.compact",
	"fold.cpp.comment.explicit",
	"fold.cpp.comment.multiline",
	"fold.cpp.explicit.anywhere",
	"fold.cpp.explicit.end",
	"fold.cpp.explicit.start",
	"fold.cpp.preprocessor.at.else",
	"fold.cpp.syntax.based",
	"fold.d.comment.explicit",
	"fold.d.comment.multiline",
	"fold.d.explicit.anywhere",
	"fold.d.explicit.end",
	"fold.d.explicit.start",
	"fold.d.syntax.based",
	"fold.directive",
	"fold.haskell.imports",
	"fold.html",
	"fold.html.preprocessor",
	"fold.hypertext.comment",
	"fold.hypertext.heredoc",
	"fold.perl.at.else",
	"fold.perl.comment.explicit",
	"fold.perl.package",
	"fold.perl.pod",
	"fold.preprocessor",
	"fold.quotes.nimrod",
	"fold.quotes.python",
	"fold.rust.comment.explicit",
	"fold.rust.comment.multiline",
	"fold.rust.explicit.anywhere",
	"fold.rust.explicit.end",
	"fold.rust.explicit.start",
	"fold.rust.syntax.based",
	"fold.sql.at.else",
	"fold.sql.only.begin",
	"fold.verilog.flags",
	"html.tags.case.sensitive",
	"lexer.asm.comment.delimiter",
	"lexer.baan.styling.within.preprocessor",
	"lexer.caml.magic",
	"lexer.cpp.allow.dollars",
	"lexer.cpp.backquoted.strings",
	"lexer.cpp.escape.sequence",
	"lexer.cpp.hashquoted.strings",
	"lexer.cpp.track.preprocessor",
	"lexer.cpp.triplequoted.strings",
	"lexer.cpp.update.preprocessor",
	"lexer.cpp.verbatim.strings.allow.escapes",
	"lexer.css.hss.language",
	"lexer.css.less.language",
	"lexer.css.scss.language",
	"lexer.d.fold.at.else",
	"lexer.edifact.highlight.un.all",
	"lexer.errorlist.escape.sequences",
	"lexer.errorlist.value.separate",
	"lexer.flagship.styling.within.preprocessor",
	"lexer.haskell.allow.hash",
	"lexer.haskell.allow.questionmark",
	"lexer.haskell.allow.quotes",
	"lexer.haskell.cpp",
	"lexer.haskell.import.safe",
	"lexer.html.django",
	"lexer.html.mako",
	"lexer.json.allow.comments",
	"lexer.json.escape.sequence",
	"lexer.metapost.comment.process",
	"lexer.metapost.interface.default",
	"lexer.pascal.smart.highlighting",
	"lexer.props_.allow.initial.spaces",
	"lexer.python.keywords2.no.sub.identifiers",
	"lexer.python.literals.binary",
	"lexer.python.strings.b",
	"lexer.python.strings.f",
	"lexer.python.strings.over.newline",
	"lexer.python.strings.u",
	"lexer.python.unicode.identifiers",
	"lexer.rust.fold.at.else",
	"lexer.sql.allow.dotted.word",
	"lexer.sql.backticks.identifier",
	"lexer.sql.numbersign.comment",
	"lexer.tex.auto.if",
	"lexer.tex.comment.process",
	"lexer.tex.interface.default",
	"lexer.tex.use.keywords",
	"lexer.verilog.allupperkeywords",
	"lexer.verilog.fold.preprocessor.else",
	"lexer.verilog.portstyling",
	"lexer.verilog.track.preprocessor",
	"lexer.verilog.update.preprocessor",
	"lexer.xml.allow.scripts",
	"nsis.ignorecase",
	"nsis.uservars",
	"ps.level",
	"sql.backslash.escapes",
	"styling.within.preprocessor",
	"tab.timmy.whinge.level",

//--Autogenerated -- end of automatically generated section

	0,
};

/* XPM */
static const char *bookmarkBluegem[] = {
/* width height num_colors chars_per_pixel */
"    15    15      64            1",
/* colors */
"  c none",
". c #0c0630",
"# c #8c8a8c",
"a c #244a84",
"b c #545254",
"c c #cccecc",
"d c #949594",
"e c #346ab4",
"f c #242644",
"g c #3c3e3c",
"h c #6ca6fc",
"i c #143789",
"j c #204990",
"k c #5c8dec",
"l c #707070",
"m c #3c82dc",
"n c #345db4",
"o c #619df7",
"p c #acacac",
"q c #346ad4",
"r c #1c3264",
"s c #174091",
"t c #5482df",
"u c #4470c4",
"v c #2450a0",
"w c #14162c",
"x c #5c94f6",
"y c #b7b8b7",
"z c #646464",
"A c #3c68b8",
"B c #7cb8fc",
"C c #7c7a7c",
"D c #3462b9",
"E c #7c7eac",
"F c #44464c",
"G c #a4a4a4",
"H c #24224c",
"I c #282668",
"J c #5c5a8c",
"K c #7c8ebc",
"L c #dcd7e4",
"M c #141244",
"N c #1c2e5c",
"O c #24327c",
"P c #4472cc",
"Q c #6ca2fc",
"R c #74b2fc",
"S c #24367c",
"T c #b4b2c4",
"U c #403e58",
"V c #4c7fd6",
"W c #24428c",
"X c #747284",
"Y c #142e7c",
"Z c #64a2fc",
"0 c #3c72dc",
"1 c #bcbebc",
"2 c #6c6a6c",
"3 c #848284",
"4 c #2c5098",
"5 c #1c1a1c",
"6 c #243250",
"7 c #7cbefc",
"8 c #d4d2d4",
/* pixels */
"    yCbgbCy    ",
"   #zGGyGGz#   ",
"  #zXTLLLTXz#  ",
" p5UJEKKKEJU5p ",
" lfISa444aSIfl ",
" wIYij444jsYIw ",
" .OsvnAAAnvsO. ",
" MWvDuVVVPDvWM ",
" HsDPVkxxtPDsH ",
" UiAtxohZxtuiU ",
" pNnkQRBRhkDNp ",
" 1FrqoR7Bo0rF1 ",
" 8GC6aemea6CG8 ",
"  cG3l2z2l3Gc  ",
"    1GdddG1    "
};

std::string SciTEBase::GetFileNameProperty(const char *name) {
	std::string namePlusDot = name;
	namePlusDot.append(".");
	std::string valueForFileName = props_.GetNewExpandString(namePlusDot.c_str(),
	        ExtensionFileName().c_str());
	if (valueForFileName.length() != 0) {
		return valueForFileName;
	} else {
		return props_.GetString(name);
	}
}

void SciTEBase::ReadProperties() {
	if (extender_)
		extender_->Clear();

	const std::string fileNameForExtension = ExtensionFileName();

	std::string modulePath = props_.GetNewExpandString("lexerpath.",
	    fileNameForExtension.c_str());
	if (modulePath.length())
	    wEditor_.CallString(SCI_LOADLEXERLIBRARY, 0, modulePath.c_str());
	language_ = props_.GetNewExpandString("lexer.", fileNameForExtension.c_str());
	if (wEditor_.Call(SCI_GETDOCUMENTOPTIONS) & SC_DOCUMENTOPTION_STYLES_NONE) {
		language_ = "";
	}
	if (language_.length()) {
		if (StartsWith(language_, "script_")) {
			wEditor_.Call(SCI_SETLEXER, SCLEX_CONTAINER);
		} else if (StartsWith(language_, "lpeg_")) {
			modulePath = props_.GetNewExpandString("lexerpath.*.lpeg");
			if (modulePath.length()) {
				wEditor_.CallString(SCI_LOADLEXERLIBRARY, 0, modulePath.c_str());
				wEditor_.CallString(SCI_SETLEXERLANGUAGE, 0, "lpeg");
				lexLPeg_ = wEditor_.Call(SCI_GETLEXER);
				const char *lexer = language_.c_str() + language_.find('_') + 1;
				wEditor_.CallReturnPointer(SCI_PRIVATELEXERCALL, SCI_SETLEXERLANGUAGE,
					SptrFromString(lexer));
			}
		} else {
			wEditor_.CallString(SCI_SETLEXERLANGUAGE, 0, language_.c_str());
		}
	} else {
		wEditor_.Call(SCI_SETLEXER, SCLEX_NULL);
	}

	props_.Set("Language", language_.c_str());

	lexLanguage_ = wEditor_.Call(SCI_GETLEXER);

	wOutput_.Call(SCI_SETLEXER, SCLEX_ERRORLIST);

	const std::string kw0 = props_.GetNewExpandString("keywords.", fileNameForExtension.c_str());
	wEditor_.CallString(SCI_SETKEYWORDS, 0, kw0.c_str());

	for (int wl = 1; wl <= KEYWORDSET_MAX; wl++) {
		std::string kwk = StdStringFromInteger(wl+1);
		kwk += '.';
		kwk.insert(0, "keywords");
		const std::string kw = props_.GetNewExpandString(kwk.c_str(), fileNameForExtension.c_str());
		wEditor_.CallString(SCI_SETKEYWORDS, wl, kw.c_str());
	}

	subStyleBases_.clear();
	const int lenSSB = wEditor_.CallString(SCI_GETSUBSTYLEBASES, 0, NULL);
	if (lenSSB) {
		wEditor_.Call(SCI_FREESUBSTYLES);

		subStyleBases_.resize(lenSSB+1);
		wEditor_.CallString(SCI_GETSUBSTYLEBASES, 0, &subStyleBases_[0]);
		subStyleBases_.resize(lenSSB);	// Remove NUL

		for (int baseStyle=0;baseStyle<lenSSB;baseStyle++) {
			//substyles.cpp.11=2
			std::string ssSubStylesKey = "substyles.";
			ssSubStylesKey += language_;
			ssSubStylesKey += ".";
			ssSubStylesKey += StdStringFromInteger(subStyleBases_[baseStyle]);
			std::string ssNumber = props_.GetNewExpandString(ssSubStylesKey.c_str());
			int subStyleIdentifiers = atoi(ssNumber.c_str());

			int subStyleIdentifiersStart = 0;
			if (subStyleIdentifiers) {
				subStyleIdentifiersStart = wEditor_.Call(SCI_ALLOCATESUBSTYLES, subStyleBases_[baseStyle], subStyleIdentifiers);
				if (subStyleIdentifiersStart < 0)
					subStyleIdentifiers = 0;
			}
			for (int subStyle=0; subStyle<subStyleIdentifiers; subStyle++) {
				// substylewords.11.1.$(file.patterns.cpp)=CharacterSet LexAccessor SString WordList
				std::string ssWordsKey = "substylewords.";
				ssWordsKey += StdStringFromInteger(subStyleBases_[baseStyle]);
				ssWordsKey += ".";
				ssWordsKey += StdStringFromInteger(subStyle + 1);
				ssWordsKey += ".";
				std::string ssWords = props_.GetNewExpandString(ssWordsKey.c_str(), fileNameForExtension.c_str());
				wEditor_.CallString(SCI_SETIDENTIFIERS, subStyleIdentifiersStart + subStyle, ssWords.c_str());
			}
		}
	}

	FilePath homepath = GetSciteDefaultHome();
	props_.Set("SciteDefaultHome", homepath.AsUTF8().c_str());
	homepath = GetSciteUserHome();
	props_.Set("SciteUserHome", homepath.AsUTF8().c_str());

	for (size_t i=0; propertiesToForward[i]; i++) {
		ForwardPropertyToEditor(propertiesToForward[i]);
	}

	if (apisFileNames_ != props_.GetNewExpandString("api.", fileNameForExtension.c_str())) {
		apis_.Clear();
		ReadAPI(fileNameForExtension);
		apisFileNames_ = props_.GetNewExpandString("api.", fileNameForExtension.c_str());
	}

	props_.Set("APIPath", apisFileNames_.c_str());

	FilePath fileAbbrev = GUI::StringFromUTF8(props_.GetNewExpandString("abbreviations.", fileNameForExtension.c_str()));
	if (!fileAbbrev.IsSet())
		fileAbbrev = GetAbbrevPropertiesFileName();
	if (!pathAbbreviations_.SameNameAs(fileAbbrev)) {
		pathAbbreviations_ = fileAbbrev;
		ReadAbbrevPropFile();
	}

	props_.Set("AbbrevPath", pathAbbreviations_.AsUTF8().c_str());

	const int tech = props_.GetInt("technology");
	wEditor_.Call(SCI_SETTECHNOLOGY, tech);
	wOutput_.Call(SCI_SETTECHNOLOGY, tech);

	const int bidirectional = props_.GetInt("bidirectional");
	wEditor_.Call(SCI_SETBIDIRECTIONAL, bidirectional);
	wOutput_.Call(SCI_SETBIDIRECTIONAL, bidirectional);

	codePage_ = props_.GetInt("code.page");
	if (CurrentBuffer()->unicodeMode != uni8Bit) {
		// Override properties file to ensure Unicode displayed.
		codePage_ = SC_CP_UTF8;
	}
	wEditor_.Call(SCI_SETCODEPAGE, codePage_);
	const int outputCodePage = props_.GetInt("output.code.page", codePage_);
	wOutput_.Call(SCI_SETCODEPAGE, outputCodePage);

	characterSet_ = props_.GetInt("character.set", SC_CHARSET_DEFAULT);

#ifdef __unix__
	const std::string localeCType = props_.GetString("LC_CTYPE");
	if (localeCType.length())
		setlocale(LC_CTYPE, localeCType.c_str());
	else
		setlocale(LC_CTYPE, "C");
#endif

	std::string imeInteraction = props_.GetString("ime.interaction");
	if (imeInteraction.length()) {
		CallChildren(SCI_SETIMEINTERACTION, props_.GetInt("ime.interaction", SC_IME_WINDOWED));
	}
	imeAutoComplete_ = props_.GetInt("ime.autocomplete", 0) == 1;

	const int accessibility = props_.GetInt("accessibility", 1);
	wEditor_.Call(SCI_SETACCESSIBILITY, accessibility);
	wOutput_.Call(SCI_SETACCESSIBILITY, accessibility);

	wrapStyle_ = props_.GetInt("wrap.style", SC_WRAP_WORD);

	CallChildren(SCI_SETCARETFORE,
	           ColourOfProperty(props_, "caret.fore", ColourRGB(0, 0, 0)));

	CallChildren(SCI_SETMOUSESELECTIONRECTANGULARSWITCH, props_.GetInt("selection.rectangular.switch.mouse", 0));
	CallChildren(SCI_SETMULTIPLESELECTION, props_.GetInt("selection.multiple", 1));
	CallChildren(SCI_SETADDITIONALSELECTIONTYPING, props_.GetInt("selection.additional.typing", 1));
	CallChildren(SCI_SETMULTIPASTE, props_.GetInt("selection.multipaste", 1));
	CallChildren(SCI_SETADDITIONALCARETSBLINK, props_.GetInt("caret.additional.blinks", 1));
	CallChildren(SCI_SETVIRTUALSPACEOPTIONS, props_.GetInt("virtual.space"));

	wEditor_.Call(SCI_SETMOUSEDWELLTIME,
	           props_.GetInt("dwell.period", SC_TIME_FOREVER), 0);

	wEditor_.Call(SCI_SETCARETWIDTH, props_.GetInt("caret.width", 1));
	wOutput_.Call(SCI_SETCARETWIDTH, props_.GetInt("caret.width", 1));

	std::string caretLineBack = props_.GetExpandedString("caret.line.back");
	if (caretLineBack.length()) {
		wEditor_.Call(SCI_SETCARETLINEVISIBLE, 1);
		wEditor_.Call(SCI_SETCARETLINEBACK, ColourFromString(caretLineBack));
	} else {
		wEditor_.Call(SCI_SETCARETLINEVISIBLE, 0);
	}
	wEditor_.Call(SCI_SETCARETLINEBACKALPHA,
		props_.GetInt("caret.line.back.alpha", SC_ALPHA_NOALPHA));

	alphaIndicator_ = props_.GetInt("indicators.alpha", 30);
	if (alphaIndicator_ < 0 || 255 < alphaIndicator_) // If invalid value,
		alphaIndicator_ = 30; //then set default value.
	underIndicator_ = props_.GetInt("indicators.under", 0) == 1;

	closeFind = static_cast<CloseFind>(props_.GetInt("find.close.on.find", 1));

	const std::string controlCharSymbol = props_.GetString("control.char.symbol");
	if (controlCharSymbol.length()) {
		wEditor_.Call(SCI_SETCONTROLCHARSYMBOL, static_cast<unsigned char>(controlCharSymbol[0]));
	} else {
		wEditor_.Call(SCI_SETCONTROLCHARSYMBOL, 0);
	}

	const std::string caretPeriod = props_.GetString("caret.period");
	if (caretPeriod.length()) {
		wEditor_.Call(SCI_SETCARETPERIOD, atoi(caretPeriod.c_str()));
		wOutput_.Call(SCI_SETCARETPERIOD, atoi(caretPeriod.c_str()));
	}

	int caretSlop = props_.GetInt("caret.policy.xslop", 1) ? CARET_SLOP : 0;
	int caretZone = props_.GetInt("caret.policy.width", 50);
	int caretStrict = props_.GetInt("caret.policy.xstrict") ? CARET_STRICT : 0;
	int caretEven = props_.GetInt("caret.policy.xeven", 1) ? CARET_EVEN : 0;
	int caretJumps = props_.GetInt("caret.policy.xjumps") ? CARET_JUMPS : 0;
	wEditor_.Call(SCI_SETXCARETPOLICY, caretStrict | caretSlop | caretEven | caretJumps, caretZone);

	caretSlop = props_.GetInt("caret.policy.yslop", 1) ? CARET_SLOP : 0;
	caretZone = props_.GetInt("caret.policy.lines");
	caretStrict = props_.GetInt("caret.policy.ystrict") ? CARET_STRICT : 0;
	caretEven = props_.GetInt("caret.policy.yeven", 1) ? CARET_EVEN : 0;
	caretJumps = props_.GetInt("caret.policy.yjumps") ? CARET_JUMPS : 0;
	wEditor_.Call(SCI_SETYCARETPOLICY, caretStrict | caretSlop | caretEven | caretJumps, caretZone);

	const int visibleStrict = props_.GetInt("visible.policy.strict") ? VISIBLE_STRICT : 0;
	const int visibleSlop = props_.GetInt("visible.policy.slop", 1) ? VISIBLE_SLOP : 0;
	const int visibleLines = props_.GetInt("visible.policy.lines");
	wEditor_.Call(SCI_SETVISIBLEPOLICY, visibleStrict | visibleSlop, visibleLines);

	wEditor_.Call(SCI_SETEDGECOLUMN, props_.GetInt("edge.column", 0));
	wEditor_.Call(SCI_SETEDGEMODE, props_.GetInt("edge.mode", EDGE_NONE));
	wEditor_.Call(SCI_SETEDGECOLOUR,
	           ColourOfProperty(props_, "edge.colour", ColourRGB(0xff, 0xda, 0xda)));

	std::string selFore = props_.GetExpandedString("selection.fore");
	if (selFore.length()) {
		CallChildren(SCI_SETSELFORE, 1, ColourFromString(selFore));
	} else {
		CallChildren(SCI_SETSELFORE, 0, 0);
	}
	std::string selBack = props_.GetExpandedString("selection.back");
	if (selBack.length()) {
		CallChildren(SCI_SETSELBACK, 1, ColourFromString(selBack));
	} else {
		if (selFore.length())
			CallChildren(SCI_SETSELBACK, 0, 0);
		else	// Have to show selection somehow
			CallChildren(SCI_SETSELBACK, 1, ColourRGB(0xC0, 0xC0, 0xC0));
	}
	const int selectionAlpha = props_.GetInt("selection.alpha", SC_ALPHA_NOALPHA);
	CallChildren(SCI_SETSELALPHA, selectionAlpha);

	std::string selAdditionalFore = props_.GetString("selection.additional.fore");
	if (selAdditionalFore.length()) {
		CallChildren(SCI_SETADDITIONALSELFORE, ColourFromString(selAdditionalFore));
	}
	std::string selAdditionalBack = props_.GetString("selection.additional.back");
	if (selAdditionalBack.length()) {
		CallChildren(SCI_SETADDITIONALSELBACK, ColourFromString(selAdditionalBack));
	}
	const int selectionAdditionalAlpha = (selectionAlpha == SC_ALPHA_NOALPHA) ? SC_ALPHA_NOALPHA : selectionAlpha / 2;
	CallChildren(SCI_SETADDITIONALSELALPHA, props_.GetInt("selection.additional.alpha", selectionAdditionalAlpha));

	foldColour_ = props_.GetExpandedString("fold.margin.colour");
	if (foldColour_.length()) {
		CallChildren(SCI_SETFOLDMARGINCOLOUR, 1, ColourFromString(foldColour_));
	} else {
		CallChildren(SCI_SETFOLDMARGINCOLOUR, 0, 0);
	}
	foldHiliteColour_ = props_.GetExpandedString("fold.margin.highlight.colour");
	if (foldHiliteColour_.length()) {
		CallChildren(SCI_SETFOLDMARGINHICOLOUR, 1, ColourFromString(foldHiliteColour_));
	} else {
		CallChildren(SCI_SETFOLDMARGINHICOLOUR, 0, 0);
	}

	std::string whitespaceFore = props_.GetExpandedString("whitespace.fore");
	if (whitespaceFore.length()) {
		CallChildren(SCI_SETWHITESPACEFORE, 1, ColourFromString(whitespaceFore));
	} else {
		CallChildren(SCI_SETWHITESPACEFORE, 0, 0);
	}
	std::string whitespaceBack = props_.GetExpandedString("whitespace.back");
	if (whitespaceBack.length()) {
		CallChildren(SCI_SETWHITESPACEBACK, 1, ColourFromString(whitespaceBack));
	} else {
		CallChildren(SCI_SETWHITESPACEBACK, 0, 0);
	}

	char bracesStyleKey[200];
	sprintf(bracesStyleKey, "braces.%s.style", language_.c_str());
	bracesStyle_ = props_.GetInt(bracesStyleKey, 0);

	char key[200];
	std::string sval;

	sval = FindLanguageProperty("calltip.*.ignorecase");
	callTipIgnoreCase_ = sval == "1";
	sval = FindLanguageProperty("calltip.*.use.escapes");
	callTipUseEscapes_ = sval == "1";

	calltipWordCharacters_ = FindLanguageProperty("calltip.*.word.characters",
		"_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
	calltipParametersStart_ = FindLanguageProperty("calltip.*.parameters.start", "(");
	calltipParametersEnd_ = FindLanguageProperty("calltip.*.parameters.end", ")");
	calltipParametersSeparators_ = FindLanguageProperty("calltip.*.parameters.separators", ",;");

	calltipEndDefinition_ = FindLanguageProperty("calltip.*.end.definition");

	sprintf(key, "autocomplete.%s.start.characters", language_.c_str());
	autoCompleteStartCharacters_ = props_.GetExpandedString(key);
	if (autoCompleteStartCharacters_ == "")
		autoCompleteStartCharacters_ = props_.GetExpandedString("autocomplete.*.start.characters");
	// "" is a quite reasonable value for this setting

	sprintf(key, "autocomplete.%s.fillups", language_.c_str());
	autoCompleteFillUpCharacters_ = props_.GetExpandedString(key);
	if (autoCompleteFillUpCharacters_ == "")
		autoCompleteFillUpCharacters_ =
			props_.GetExpandedString("autocomplete.*.fillups");
	wEditor_.CallString(SCI_AUTOCSETFILLUPS, 0,
		autoCompleteFillUpCharacters_.c_str());

	sprintf(key, "autocomplete.%s.typesep", language_.c_str());
	autoCompleteTypeSeparator_ = props_.GetExpandedString(key);
	if (autoCompleteTypeSeparator_ == "")
		autoCompleteTypeSeparator_ =
			props_.GetExpandedString("autocomplete.*.typesep");
	if (autoCompleteTypeSeparator_.length()) {
		wEditor_.Call(SCI_AUTOCSETTYPESEPARATOR,
			static_cast<unsigned char>(autoCompleteTypeSeparator_[0]));
	}

	sprintf(key, "autocomplete.%s.ignorecase", "*");
	sval = props_.GetNewExpandString(key);
	autoCompleteIgnoreCase_ = sval == "1";
	sprintf(key, "autocomplete.%s.ignorecase", language_.c_str());
	sval = props_.GetNewExpandString(key);
	if (sval != "")
		autoCompleteIgnoreCase_ = sval == "1";
	wEditor_.Call(SCI_AUTOCSETIGNORECASE, autoCompleteIgnoreCase_ ? 1 : 0);
	wOutput_.Call(SCI_AUTOCSETIGNORECASE, 1);

	const int autoCChooseSingle = props_.GetInt("autocomplete.choose.single");
	wEditor_.Call(SCI_AUTOCSETCHOOSESINGLE, autoCChooseSingle);

	wEditor_.Call(SCI_AUTOCSETCANCELATSTART, 0);
	wEditor_.Call(SCI_AUTOCSETDROPRESTOFWORD, 0);

	if (firstPropertiesRead_) {
		ReadPropertiesInitial();
	}

	ReadFontProperties();

	wEditor_.Call(SCI_SETPRINTMAGNIFICATION, props_.GetInt("print.magnification"));
	wEditor_.Call(SCI_SETPRINTCOLOURMODE, props_.GetInt("print.colour.mode"));

	jobQueue_.clearBeforeExecute = props_.GetInt("clear.before.execute");
	jobQueue_.timeCommands = props_.GetInt("time.commands");

	const int blankMarginLeft = props_.GetInt("blank.margin.left", 1);
	const int blankMarginLeftOutput = props_.GetInt("output.blank.margin.left", blankMarginLeft);
	const int blankMarginRight = props_.GetInt("blank.margin.right", 1);
	wEditor_.Call(SCI_SETMARGINLEFT, 0, blankMarginLeft);
	wEditor_.Call(SCI_SETMARGINRIGHT, 0, blankMarginRight);
	wOutput_.Call(SCI_SETMARGINLEFT, 0, blankMarginLeftOutput);
	wOutput_.Call(SCI_SETMARGINRIGHT, 0, blankMarginRight);

	marginWidth_ = props_.GetInt("margin.width");
	if (marginWidth_ == 0)
		marginWidth_ = kMarginWidthDefault;
	wEditor_.Call(SCI_SETMARGINWIDTHN, 1, margin_ ? marginWidth_ : 0);

	const std::string lineMarginProp = props_.GetString("line.margin.width");
	lineNumbersWidth_ = atoi(lineMarginProp.c_str());
	if (lineNumbersWidth_ == 0)
		lineNumbersWidth_ = kLineNumbersWidthDefault;
	lineNumbersExpand_ = lineMarginProp.find('+') != std::string::npos;

	SetLineNumberWidth();

	bufferedDraw_ = props_.GetInt("buffered.draw");
	wEditor_.Call(SCI_SETBUFFEREDDRAW, bufferedDraw_);
	wOutput_.Call(SCI_SETBUFFEREDDRAW, bufferedDraw_);

	const int phasesDraw = props_.GetInt("phases.draw", 1);
	wEditor_.Call(SCI_SETPHASESDRAW, phasesDraw);
	wOutput_.Call(SCI_SETPHASESDRAW, phasesDraw);

	wEditor_.Call(SCI_SETLAYOUTCACHE, props_.GetInt("cache.layout", SC_CACHE_CARET));
	wOutput_.Call(SCI_SETLAYOUTCACHE, props_.GetInt("output.cache.layout", SC_CACHE_CARET));

	bracesCheck_ = props_.GetInt("braces.check");
	bracesSloppy_ = props_.GetInt("braces.sloppy");

	wEditor_.Call(SCI_SETCHARSDEFAULT);
	wordCharacters_ = props_.GetNewExpandString("word.characters.", fileNameForExtension.c_str());
	if (wordCharacters_.length()) {
		wEditor_.CallString(SCI_SETWORDCHARS, 0, wordCharacters_.c_str());
	} else {
		wordCharacters_ = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	}

	whitespaceCharacters_ = props_.GetNewExpandString("whitespace.characters.", fileNameForExtension.c_str());
	if (whitespaceCharacters_.length()) {
		wEditor_.CallString(SCI_SETWHITESPACECHARS, 0, whitespaceCharacters_.c_str());
	}

	const std::string viewIndentExamine = GetFileNameProperty("view.indentation.examine");
	indentExamine_ = viewIndentExamine.length() ? (atoi(viewIndentExamine.c_str())) : SC_IV_REAL;
	wEditor_.Call(SCI_SETINDENTATIONGUIDES, props_.GetInt("view.indentation.guides") ?
		indentExamine_ : SC_IV_NONE);

	wEditor_.Call(SCI_SETTABINDENTS, props_.GetInt("tab.indents", 1));
	wEditor_.Call(SCI_SETBACKSPACEUNINDENTS, props_.GetInt("backspace.unindents", 1));

	wEditor_.Call(SCI_CALLTIPUSESTYLE, 32);

	std::string useStripTrailingSpaces = props_.GetNewExpandString("strip.trailing.spaces.", ExtensionFileName().c_str());
	if (useStripTrailingSpaces.length() > 0) {
		stripTrailingSpaces_ = atoi(useStripTrailingSpaces.c_str()) != 0;
	} else {
		stripTrailingSpaces_ = props_.GetInt("strip.trailing.spaces") != 0;
	}
	ensureFinalLineEnd_ = props_.GetInt("ensure.final.line.end") != 0;
	ensureConsistentLineEnds_ = props_.GetInt("ensure.consistent.line.ends") != 0;

	indentOpening_ = props_.GetInt("indent.opening");
	indentClosing_ = props_.GetInt("indent.closing");
	indentMaintain_ = atoi(props_.GetNewExpandString("indent.maintain.", fileNameForExtension.c_str()).c_str());

	const std::string lookback = props_.GetNewExpandString("statement.lookback.", fileNameForExtension.c_str());
	statementLookback_ = atoi(lookback.c_str());
	statementIndent_ = GetStyleAndWords("statement.indent.");
	statementEnd_ = GetStyleAndWords("statement.end.");
	blockStart_ = GetStyleAndWords("block.start.");
	blockEnd_ = GetStyleAndWords("block.end.");

	struct PropToPPC {
		const char *propName;
		PreProcKind ppc;
	};
	PropToPPC propToPPC[] = {
		{"preprocessor.start.", kPpcStart},
		{"preprocessor.middle.", kPpcMiddle},
		{"preprocessor.end.", kPpcEnd},
	};
	const std::string ppSymbol = props_.GetNewExpandString("preprocessor.symbol.", fileNameForExtension.c_str());
	preprocessorSymbol_ = ppSymbol.empty() ? 0 : ppSymbol[0];
	preprocOfString_.clear();
	for (const PropToPPC &preproc : propToPPC) {
		const std::string list = props_.GetNewExpandString(preproc.propName, fileNameForExtension.c_str());
		const std::vector<std::string> words = StringSplit(list, ' ');
		for (const std::string &word : words) {
			preprocOfString_[word] = preproc.ppc;
		}
	}

	memFiles_.AppendList(props_.GetNewExpandString("find.files").c_str());

	wEditor_.Call(SCI_SETWRAPVISUALFLAGS, props_.GetInt("wrap.visual.flags"));
	wEditor_.Call(SCI_SETWRAPVISUALFLAGSLOCATION, props_.GetInt("wrap.visual.flags.location"));
 	wEditor_.Call(SCI_SETWRAPSTARTINDENT, props_.GetInt("wrap.visual.startindent"));
 	wEditor_.Call(SCI_SETWRAPINDENTMODE, props_.GetInt("wrap.indent.mode"));

	wEditor_.Call(SCI_SETIDLESTYLING, props_.GetInt("idle.styling", SC_IDLESTYLING_NONE));
	wOutput_.Call(SCI_SETIDLESTYLING, props_.GetInt("output.idle.styling", SC_IDLESTYLING_NONE));

	if (props_.GetInt("os.x.home.end.keys")) {
		AssignKey(SCK_HOME, 0, SCI_SCROLLTOSTART);
		AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_NULL);
		AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_NULL);
		AssignKey(SCK_END, 0, SCI_SCROLLTOEND);
		AssignKey(SCK_END, SCMOD_SHIFT, SCI_NULL);
	} else {
		if (props_.GetInt("wrap.aware.home.end.keys",0)) {
			if (props_.GetInt("vc.home.key", 1)) {
				AssignKey(SCK_HOME, 0, SCI_VCHOMEWRAP);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_VCHOMEWRAPEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_VCHOMERECTEXTEND);
			} else {
				AssignKey(SCK_HOME, 0, SCI_HOMEWRAP);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_HOMEWRAPEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_HOMERECTEXTEND);
			}
			AssignKey(SCK_END, 0, SCI_LINEENDWRAP);
			AssignKey(SCK_END, SCMOD_SHIFT, SCI_LINEENDWRAPEXTEND);
		} else {
			if (props_.GetInt("vc.home.key", 1)) {
				AssignKey(SCK_HOME, 0, SCI_VCHOME);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_VCHOMEEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_VCHOMERECTEXTEND);
			} else {
				AssignKey(SCK_HOME, 0, SCI_HOME);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_HOMEEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_HOMERECTEXTEND);
			}
			AssignKey(SCK_END, 0, SCI_LINEEND);
			AssignKey(SCK_END, SCMOD_SHIFT, SCI_LINEENDEXTEND);
		}
	}

	AssignKey('L', SCMOD_SHIFT | SCMOD_CTRL, SCI_LINEDELETE);


	scrollOutput_ = props_.GetInt("output.scroll", 1);

	tabHideOne_ = props_.GetInt("tabbar.hide.one");

	SetToolsMenu();

	wEditor_.Call(SCI_SETFOLDFLAGS, props_.GetInt("fold.flags"));

	// To put the folder markers in the line number region
	//wEditor_.Call(SCI_SETMARGINMASKN, 0, SC_MASK_FOLDERS);

	wEditor_.Call(SCI_SETMODEVENTMASK, SC_MOD_CHANGEFOLD);

	if (0==props_.GetInt("undo.redo.lazy")) {
		// Trap for insert/delete notifications (also fired by undo
		// and redo) so that the buttons can be enabled if needed.
		wEditor_.Call(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT
			| SC_LASTSTEPINUNDOREDO | wEditor_.Call(SCI_GETMODEVENTMASK, 0));

		//SC_LASTSTEPINUNDOREDO is probably not needed in the mask; it
		//doesn't seem to fire as an event of its own; just modifies the
		//insert and delete events.
	}

	// Create a margin column for the folding symbols
	wEditor_.Call(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);

	foldMarginWidth_ = props_.GetInt("fold.margin.width");
	if (foldMarginWidth_ == 0)
		foldMarginWidth_ = kFoldMarginWidthDefault;
	wEditor_.Call(SCI_SETMARGINWIDTHN, 2, foldMargin_ ? foldMarginWidth_ : 0);

	wEditor_.Call(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
	wEditor_.Call(SCI_SETMARGINSENSITIVEN, 2, 1);

	// Define foreground (outline) and background (fill) color of folds
	const int foldSymbols = props_.GetInt("fold.symbols");
	std::string foldFore = props_.GetExpandedString("fold.fore");
	if (foldFore.length() == 0) {
		// Set default colour for outline
		switch (foldSymbols) {
		case 0: // Arrows
			foldFore = "#000000";
			break;
		case 1: // + -
			foldFore = "#FFFFFF";
			break;
		case 2: // Circles
			foldFore = "#404040";
			break;
		case 3: // Squares
			foldFore = "#808080";
			break;
		}
	}
	const Colour colourFoldFore = ColourFromString(foldFore);

	std::string foldBack = props_.GetExpandedString("fold.back");
	// Set default colour for fill
	if (foldBack.length() == 0) {
		switch (foldSymbols) {
		case 0:
		case 1:
			foldBack = "#000000";
			break;
		case 2:
		case 3:
			foldBack = "#FFFFFF";
			break;
		}
	}
	const Colour colourFoldBack = ColourFromString(foldBack);

	// Enable/disable highlight for current folding block (smallest one that contains the caret)
	const int isHighlightEnabled = props_.GetInt("fold.highlight", 0);
	// Define the colour of highlight
	const Colour colourFoldBlockHighlight = ColourOfProperty(props_, "fold.highlight.colour", ColourRGB(0xFF, 0, 0));

	switch (foldSymbols) {
	case 0:
		// Arrow pointing right for contracted folders, arrow pointing down for expanded
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_ARROWDOWN,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_ARROW,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		// The highlight is disabled for arrow.
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, false);
		break;
	case 1:
		// Plus for contracted folders, minus for expanded
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_MINUS,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_PLUS,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		// The highlight is disabled for plus/minus.
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, false);
		break;
	case 2:
		// Like a flattened tree control using circular headers and curved joins
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_CIRCLEMINUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_CIRCLEPLUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNERCURVE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_CIRCLEPLUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_CIRCLEMINUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNERCURVE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, isHighlightEnabled);
		break;
	case 3:
		// Like a flattened tree control using square headers
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, isHighlightEnabled);
		break;
	}

	wEditor_.Call(SCI_MARKERSETFORE, kMarkerBookmark,
		ColourOfProperty(props_, "bookmark.fore", ColourRGB(0xbe, 0, 0)));
	wEditor_.Call(SCI_MARKERSETBACK, kMarkerBookmark,
		ColourOfProperty(props_, "bookmark.back", ColourRGB(0xe2, 0x40, 0x40)));
	wEditor_.Call(SCI_MARKERSETALPHA, kMarkerBookmark,
		props_.GetInt("bookmark.alpha", SC_ALPHA_NOALPHA));
	const std::string bookMarkXPM = props_.GetString("bookmark.pixmap");
	if (bookMarkXPM.length()) {
		wEditor_.CallString(SCI_MARKERDEFINEPIXMAP, kMarkerBookmark,
			bookMarkXPM.c_str());
	} else if (props_.GetString("bookmark.fore").length()) {
		wEditor_.Call(SCI_MARKERDEFINE, kMarkerBookmark, props_.GetInt("bookmark.symbol", SC_MARK_BOOKMARK));
	} else {
		// No bookmark.fore setting so display default pixmap.
		wEditor_.CallPointer(SCI_MARKERDEFINEPIXMAP, kMarkerBookmark, bookmarkBluegem);
	}

	wEditor_.Call(SCI_SETSCROLLWIDTH, props_.GetInt("horizontal.scroll.width", 2000));
	wEditor_.Call(SCI_SETSCROLLWIDTHTRACKING, props_.GetInt("horizontal.scroll.width.tracking", 1));
	wOutput_.Call(SCI_SETSCROLLWIDTH, props_.GetInt("output.horizontal.scroll.width", 2000));
	wOutput_.Call(SCI_SETSCROLLWIDTHTRACKING, props_.GetInt("output.horizontal.scroll.width.tracking", 1));

	// Do these last as they force a style refresh
	wEditor_.Call(SCI_SETHSCROLLBAR, props_.GetInt("horizontal.scrollbar", 1));
	wOutput_.Call(SCI_SETHSCROLLBAR, props_.GetInt("output.horizontal.scrollbar", 1));

	wEditor_.Call(SCI_SETENDATLASTLINE, props_.GetInt("end.at.last.line", 1));
	wEditor_.Call(SCI_SETCARETSTICKY, props_.GetInt("caret.sticky", 0));

	// Clear all previous indicators.
	wEditor_.Call(SCI_SETINDICATORCURRENT, kIndicatorHighlightCurrentWord);
	wEditor_.Call(SCI_INDICATORCLEARRANGE, 0, wEditor_.Call(SCI_GETLENGTH));
	wOutput_.Call(SCI_SETINDICATORCURRENT, kIndicatorHighlightCurrentWord);
	wOutput_.Call(SCI_INDICATORCLEARRANGE, 0, wOutput_.Call(SCI_GETLENGTH));
	currentWordHighlight.statesOfDelay = currentWordHighlight.kNoDelay;

	currentWordHighlight.isEnabled = props_.GetInt("highlight.current.word", 0) == 1;
	if (currentWordHighlight.isEnabled) {
		const std::string highlightCurrentWordIndicatorString = props_.GetExpandedString("highlight.current.word.indicator");
		IndicatorDefinition highlightCurrentWordIndicator(highlightCurrentWordIndicatorString.c_str());
		if (highlightCurrentWordIndicatorString.length() == 0) {
			highlightCurrentWordIndicator.style = INDIC_ROUNDBOX;
			std::string highlightCurrentWordColourString = props_.GetExpandedString("highlight.current.word.colour");
			if (highlightCurrentWordColourString.length() == 0) {
				// Set default colour for highlight.
				highlightCurrentWordColourString = "#A0A000";
			}
			highlightCurrentWordIndicator.colour = ColourFromString(highlightCurrentWordColourString);
			highlightCurrentWordIndicator.fillAlpha = alphaIndicator_;
			highlightCurrentWordIndicator.under = underIndicator_;
		}
		SetOneIndicator(wEditor_, kIndicatorHighlightCurrentWord, highlightCurrentWordIndicator);
		SetOneIndicator(wOutput_, kIndicatorHighlightCurrentWord, highlightCurrentWordIndicator);
		currentWordHighlight.isOnlyWithSameStyle = props_.GetInt("highlight.current.word.by.style", 0) == 1;
		HighlightCurrentWord(true);
	}

	std::map<std::string, std::string> eConfig = editorConfig_->MapFromAbsolutePath(filePath_);
	for (const std::pair<const std::string, std::string> &pss : eConfig) {
		if (pss.first == "indent_style") {
			wEditor_.Call(SCI_SETUSETABS, pss.second == "tab" ? 1 : 0);
		} else if (pss.first == "indent_size") {
			wEditor_.Call(SCI_SETINDENT, std::stoi(pss.second));
		} else if (pss.first == "tab_width") {
			wEditor_.Call(SCI_SETTABWIDTH, std::stoi(pss.second));
		} else if (pss.first == "end_of_line") {
			if (pss.second == "lf") {
				wEditor_.Call(SCI_SETEOLMODE, SC_EOL_LF);
			} else if (pss.second == "cr") {
				wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CR);
			} else if (pss.second == "crlf") {
				wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CRLF);
			}
		} else if (pss.first == "charset") {
			if (pss.second == "latin1") {
				CurrentBuffer()->unicodeMode = uni8Bit;
				codePage_ = 0;
			} else {
				if (pss.second == "utf-8")
					CurrentBuffer()->unicodeMode = uniCookie;
				if (pss.second == "utf-8-bom")
					CurrentBuffer()->unicodeMode = uniUTF8;
				if (pss.second == "utf-16be")
					CurrentBuffer()->unicodeMode = uni16BE;
				if (pss.second == "utf-16le")
					CurrentBuffer()->unicodeMode = uni16LE;
				codePage_ = SC_CP_UTF8;
			}
			wEditor_.Call(SCI_SETCODEPAGE, codePage_);
		} else if (pss.first == "trim_trailing_whitespace") {
			stripTrailingSpaces_ = pss.second == "true";
		} else if (pss.first == "insert_final_newline") {
			ensureFinalLineEnd_ = pss.second == "true";
		}
	}

	if (extender_) {
		FilePath defaultDir = GetDefaultDirectory();
		FilePath scriptPath;

		// Check for an extension script
		GUI::gui_string extensionFile = GUI::StringFromUTF8(
			props_.GetNewExpandString("extension.", fileNameForExtension.c_str()));
		if (extensionFile.length()) {
			// find file in local directory
			FilePath docDir = filePath_.Directory();
			if (Exists(docDir.AsInternal(), extensionFile.c_str(), &scriptPath)) {
				// Found file in document directory
				extender_->Load(scriptPath.AsUTF8().c_str());
			} else if (Exists(defaultDir.AsInternal(), extensionFile.c_str(), &scriptPath)) {
				// Found file in global directory
				extender_->Load(scriptPath.AsUTF8().c_str());
			} else if (Exists(GUI_TEXT(""), extensionFile.c_str(), &scriptPath)) {
				// Found as completely specified file name
				extender_->Load(scriptPath.AsUTF8().c_str());
			}
		}
	}

	delayBeforeAutoSave_ = props_.GetInt("save.on.timer");
	if (delayBeforeAutoSave_) {
		TimerStart(kTimerAutoSave);
	} else {
		TimerEnd(kTimerAutoSave);
	}

	firstPropertiesRead_ = false;
	needReadProperties_ = false;
}

void SciTEBase::ReadFontProperties() {
	char key[200];
	const char *languageName = language_.c_str();

	if (lexLanguage_ == lexLPeg_) {
		// Retrieve style info.
		char propStr[256];
		for (int i = 0; i < STYLE_MAX; i++) {
			sprintf(key, "style.lpeg.%0d", i);
			wEditor_.CallReturnPointer(SCI_PRIVATELEXERCALL, i - STYLE_MAX,
				SptrFromString(propStr));
			props_.Set(key, static_cast<const char *>(propStr));
		}
		languageName = "lpeg";
	}

	// Set styles
	// For each window set the global default style, then the language default style, then the other global styles, then the other language styles

	const int fontQuality = props_.GetInt("font.quality");
	wEditor_.Call(SCI_SETFONTQUALITY, fontQuality);
	wOutput_.Call(SCI_SETFONTQUALITY, fontQuality);

	wEditor_.Call(SCI_STYLERESETDEFAULT, 0, 0);
	wOutput_.Call(SCI_STYLERESETDEFAULT, 0, 0);

	sprintf(key, "style.%s.%0d", "*", STYLE_DEFAULT);
	std::string sval = props_.GetNewExpandString(key);
	SetOneStyle(wEditor_, STYLE_DEFAULT, StyleDefinition(sval));
	SetOneStyle(wOutput_, STYLE_DEFAULT, StyleDefinition(sval));

	sprintf(key, "style.%s.%0d", languageName, STYLE_DEFAULT);
	sval = props_.GetNewExpandString(key);
	SetOneStyle(wEditor_, STYLE_DEFAULT, StyleDefinition(sval));

	wEditor_.Call(SCI_STYLECLEARALL, 0, 0);

	SetStyleFor(wEditor_, "*");
	SetStyleFor(wEditor_, languageName);
	if (props_.GetInt("error.inline")) {
		wEditor_.Call(SCI_RELEASEALLEXTENDEDSTYLES, 0, 0);
		diagnosticStyleStart_ = wEditor_.Call(SCI_ALLOCATEEXTENDEDSTYLES, kDiagnosticStyles, 0);
		SetStyleBlock(wEditor_, "error", diagnosticStyleStart_, diagnosticStyleStart_+kDiagnosticStyles-1);
	}

	const int diffToSecondary = static_cast<int>(wEditor_.Call(SCI_DISTANCETOSECONDARYSTYLES));
	for (const char subStyleBase : subStyleBases_) {
		const int subStylesStart = wEditor_.Call(SCI_GETSUBSTYLESSTART, subStyleBase);
		const int subStylesLength = wEditor_.Call(SCI_GETSUBSTYLESLENGTH, subStyleBase);
		for (int subStyle=0; subStyle<subStylesLength; subStyle++) {
			for (int active=0; active<(diffToSecondary?2:1); active++) {
				const int activity = active * diffToSecondary;
				sprintf(key, "style.%s.%0d.%0d", languageName, subStyleBase + activity, subStyle+1);
				sval = props_.GetNewExpandString(key);
				SetOneStyle(wEditor_, subStylesStart + subStyle + activity, StyleDefinition(sval));
			}
		}
	}

	// Turn grey while loading
	if (CurrentBuffer()->lifeState == Buffer::kReading)
		wEditor_.Call(SCI_STYLESETBACK, STYLE_DEFAULT, 0xEEEEEE);

	wOutput_.Call(SCI_STYLECLEARALL, 0, 0);

	sprintf(key, "style.%s.%0d", "errorlist", STYLE_DEFAULT);
	sval = props_.GetNewExpandString(key);
	SetOneStyle(wOutput_, STYLE_DEFAULT, StyleDefinition(sval));

	wOutput_.Call(SCI_STYLECLEARALL, 0, 0);

	SetStyleFor(wOutput_, "*");
	SetStyleFor(wOutput_, "errorlist");

	if (CurrentBuffer()->useMonoFont) {
		sval = props_.GetExpandedString("font.monospace");
		StyleDefinition sd(sval.c_str());
		for (int style = 0; style <= STYLE_MAX; style++) {
			if (style != STYLE_LINENUMBER) {
				if (sd.specified & StyleDefinition::sdFont) {
					wEditor_.CallString(SCI_STYLESETFONT, style, sd.font.c_str());
				}
				if (sd.specified & StyleDefinition::sdSize) {
					wEditor_.Call(SCI_STYLESETSIZEFRACTIONAL, style, sd.FractionalSize());
				}
			}
		}
	}
}

// Properties that are interactively modifiable are only read from the properties file once.
void SciTEBase::SetPropertiesInitial() {
	splitVertical_ = props_.GetInt("split.vertical");
	openFilesHere_ = props_.GetInt("check.if.already.open");
	wrap_ = props_.GetInt("wrap");
	wrapOutput_ = props_.GetInt("output.wrap");
	indentationWSVisible_ = props_.GetInt("view.indentation.whitespace", 1);
	sbVisible_ = props_.GetInt("statusbar.visible");
	tbVisible_ = props_.GetInt("toolbar.visible");
	tabVisible_ = props_.GetInt("tabbar.visible");
	tabMultiLine_ = props_.GetInt("tabbar.multiline");
	lineNumbers_ = props_.GetInt("line.margin.visible");
	margin_ = props_.GetInt("margin.width");
	foldMargin_ = props_.GetInt("fold.margin.width", kFoldMarginWidthDefault);

	matchCase = props_.GetInt("find.replace.matchcase");
	regExp = props_.GetInt("find.replace.regexp");
	unSlash = props_.GetInt("find.replace.escapes");
	wrapFind = props_.GetInt("find.replace.wrap", 1);
	focusOnReplace = props_.GetInt("find.replacewith.focus", 1);
}

GUI::gui_string Localization::Text(const char *s, bool retainIfNotFound) {
	const std::string sEllipse("...");	// An ASCII ellipse
	const std::string utfEllipse("\xe2\x80\xa6");	// A UTF-8 ellipse
	std::string translation = s;
	const int ellipseIndicator = Remove(translation, sEllipse);
	const int utfEllipseIndicator = Remove(translation, utfEllipse);
	const std::string menuAccessIndicatorChar(1, static_cast<char>(menuAccessIndicator[0]));
	const int accessKeyPresent = Remove(translation, menuAccessIndicatorChar);
	LowerCaseAZ(translation);
	Substitute(translation, "\n", "\\n");
	translation = GetString(translation.c_str());
	if (translation.length()) {
		if (ellipseIndicator)
			translation += sEllipse;
		if (utfEllipseIndicator)
			translation += utfEllipse;
		if (0 == accessKeyPresent) {
#if !defined(GTK)
			// Following codes are required because accelerator is not always
			// part of alphabetical word in several language. In these cases,
			// accelerator is written like "(&O)".
			const size_t posOpenParenAnd = translation.find("(&");
			if ((posOpenParenAnd != std::string::npos) && (translation.find(")", posOpenParenAnd) == posOpenParenAnd + 3)) {
				translation.erase(posOpenParenAnd, 4);
			} else {
				Remove(translation, std::string("&"));
			}
#else
			Remove(translation, std::string("&"));
#endif
		}
		Substitute(translation, "&", menuAccessIndicatorChar);
		Substitute(translation, "\\n", "\n");
	} else {
		translation = missing;
	}
	if ((translation.length() > 0) || !retainIfNotFound) {
		return GUI::StringFromUTF8(translation);
	}
	return GUI::StringFromUTF8(s);
}

GUI::gui_string SciTEBase::LocaliseMessage(const char *s, const GUI::gui_char *param0, const GUI::gui_char *param1, const GUI::gui_char *param2) {
	GUI::gui_string translation = localiser_.Text(s);
	if (param0)
		Substitute(translation, GUI_TEXT("^0"), param0);
	if (param1)
		Substitute(translation, GUI_TEXT("^1"), param1);
	if (param2)
		Substitute(translation, GUI_TEXT("^2"), param2);
	return translation;
}

void SciTEBase::ReadLocalization() {
	localiser_.Clear();
	GUI::gui_string title = GUI_TEXT("locale.properties");
	const std::string localeProps = props_.GetExpandedString("locale.properties");
	if (localeProps.length()) {
		title = GUI::StringFromUTF8(localeProps);
	}
	FilePath propdir = GetSciteDefaultHome();
	FilePath localePath(propdir, title);
	localiser_.Read(localePath, propdir, filter_, &importFiles_, 0);
	localiser_.SetMissing(props_.GetString("translation.missing"));
	localiser_.read = true;
}

void SciTEBase::ReadPropertiesInitial() {
	SetPropertiesInitial();
	const int sizeHorizontal = props_.GetInt("output.horizontal.size", 0);
	const int sizeVertical = props_.GetInt("output.vertical.size", 0);
	const int hideOutput = props_.GetInt("output.initial.hide", 0);
	if ((!splitVertical_ && (sizeVertical > 0) && (heightOutput_ < sizeVertical)) ||
		(splitVertical_ && (sizeHorizontal > 0) && (heightOutput_ < sizeHorizontal))) {
		previousHeightOutput_ = splitVertical_ ? sizeHorizontal : sizeVertical;
		if (!hideOutput) {
			heightOutput_ = NormaliseSplit(previousHeightOutput_);
			SizeSubWindows();
			Redraw();
		}
	}
	ViewWhitespace(props_.GetInt("view.whitespace"));
	wEditor_.Call(SCI_SETINDENTATIONGUIDES, props_.GetInt("view.indentation.guides") ?
		indentExamine_ : SC_IV_NONE);

	wEditor_.Call(SCI_SETVIEWEOL, props_.GetInt("view.eol"));
	wEditor_.Call(SCI_SETZOOM, props_.GetInt("magnification"));
	wOutput_.Call(SCI_SETZOOM, props_.GetInt("output.magnification"));
	wEditor_.Call(SCI_SETWRAPMODE, wrap_ ? wrapStyle_ : SC_WRAP_NONE);
	wOutput_.Call(SCI_SETWRAPMODE, wrapOutput_ ? wrapStyle_ : SC_WRAP_NONE);

	std::string menuLanguageProp = props_.GetExpandedString("menu.language");
	std::replace(menuLanguageProp.begin(), menuLanguageProp.end(), '|', '\0');
	const char *sMenuLanguage = menuLanguageProp.c_str();
	while (*sMenuLanguage) {
		LanguageMenuItem lmi;
		lmi.menuItem = sMenuLanguage;
		sMenuLanguage += strlen(sMenuLanguage) + 1;
		lmi.extension = sMenuLanguage;
		sMenuLanguage += strlen(sMenuLanguage) + 1;
		lmi.menuKey = sMenuLanguage;
		sMenuLanguage += strlen(sMenuLanguage) + 1;
		languageMenu_.push_back(lmi);
	}
	SetLanguageMenu();

	// load the user defined short cut props_
	std::string shortCutProp = props_.GetNewExpandString("user.shortcuts");
	if (shortCutProp.length()) {
		const size_t pipes = std::count(shortCutProp.begin(), shortCutProp.end(), '|');
		std::replace(shortCutProp.begin(), shortCutProp.end(), '|', '\0');
		const char *sShortCutProp = shortCutProp.c_str();
		for (size_t item = 0; item < pipes/2; item++) {
			ShortcutItem sci;
			sci.menuKey = sShortCutProp;
			sShortCutProp += strlen(sShortCutProp) + 1;
			sci.menuCommand = sShortCutProp;
			sShortCutProp += strlen(sShortCutProp) + 1;
			shortCutItemList_.push_back(sci);
		}
	}
	// end load the user defined short cut props_

	FilePath homepath = GetSciteDefaultHome();
	props_.Set("SciteDefaultHome", homepath.AsUTF8().c_str());
	homepath = GetSciteUserHome();
	props_.Set("SciteUserHome", homepath.AsUTF8().c_str());
}

FilePath SciTEBase::GetDefaultPropertiesFileName() {
	return FilePath(GetSciteDefaultHome(), propGlobalFileName);
}

FilePath SciTEBase::GetAbbrevPropertiesFileName() {
	return FilePath(GetSciteUserHome(), propAbbrevFileName);
}

FilePath SciTEBase::GetUserPropertiesFileName() {
	return FilePath(GetSciteUserHome(), propUserFileName);
}

FilePath SciTEBase::GetLocalPropertiesFileName() {
	return FilePath(filePath_.Directory(), propLocalFileName);
}

FilePath SciTEBase::GetDirectoryPropertiesFileName() {
	FilePath propfile;

	if (filePath_.IsSet()) {
		propfile.Set(filePath_.Directory(), propDirectoryFileName);

		// if this file does not exist try to find the prop file in a parent directory
		while (!propfile.Directory().IsRoot() && !propfile.Exists()) {
			propfile.Set(propfile.Directory().Directory(), propDirectoryFileName);
		}

		// not found -> set it to the initial directory
		if (!propfile.Exists()) {
			propfile.Set(filePath_.Directory(), propDirectoryFileName);
		}
	}
	return propfile;
}

void SciTEBase::OpenProperties(int propsFile) {
	FilePath propfile;
	switch (propsFile) {
	case IDM_OPENLOCALPROPERTIES:
		propfile = GetLocalPropertiesFileName();
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENUSERPROPERTIES:
		propfile = GetUserPropertiesFileName();
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENABBREVPROPERTIES:
		propfile = pathAbbreviations_;
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENGLOBALPROPERTIES:
		propfile = GetDefaultPropertiesFileName();
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENLUAEXTERNALFILE: {
			GUI::gui_string extlua = GUI::StringFromUTF8(props_.GetExpandedString("ext.lua.startup.script"));
			if (extlua.length()) {
				Open(extlua.c_str(), kOfQuiet);
			}
			break;
		}
	case IDM_OPENDIRECTORYPROPERTIES: {
			propfile = GetDirectoryPropertiesFileName();
			const bool alreadyExists = propfile.Exists();
			Open(propfile, kOfQuiet);
			if (!alreadyExists)
				SaveAsDialog();
		}
		break;
	}
}

// return the int value of the command name passed in.
int SciTEBase::GetMenuCommandAsInt(std::string commandName) {
	int i = IFaceTable::FindConstant(commandName.c_str());
	if (i != -1) {
		return IFaceTable::constants[i].value;
	}

	// Check also for a SCI command, as long as it has no parameters
	i = IFaceTable::FindFunctionByConstantName(commandName.c_str());
	if (i != -1 &&
		IFaceTable::functions[i].paramType[0] == iface_void &&
		IFaceTable::functions[i].paramType[1] == iface_void) {
		return IFaceTable::functions[i].value;
	}

	// Otherwise we might have entered a number as command to access a "SCI_" command
	return atoi(commandName.c_str());
}
