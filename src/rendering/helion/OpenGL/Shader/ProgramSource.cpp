/*
** ProgramSource.cpp
**
** UZDoom-only. Lifts the interface declarations out of a Helion-style GLSL
** stage source.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <cstring>
#include <cstdlib>
#include "ProgramSource.h"

namespace Helion::Render::OpenGL::Shader
{

namespace
{

bool IsIdentStart(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentChar(char c)
{
	return IsIdentStart(c) || (c >= '0' && c <= '9');
}

bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool Eq(const FString& a, const char* b)
{
	return a.Compare(b) == 0;
}

bool IsSamplerType(const FString& type)
{
	return type.IndexOf("sampler") >= 0 && type.IndexOf("sampler") <= 1;	// sampler*, isampler*, usampler*
}

class Scanner
{
public:
	Scanner(const char* chars, size_t length) : m_s(chars), m_n(length) { }

	size_t SkipWhitespaceAndComments(size_t p) const
	{
		while (p < m_n)
		{
			if (IsSpace(m_s[p]))
			{
				p++;
			}
			else if (m_s[p] == '/' && p + 1 < m_n && m_s[p + 1] == '/')
			{
				while (p < m_n && m_s[p] != '\n')
					p++;
			}
			else if (m_s[p] == '/' && p + 1 < m_n && m_s[p + 1] == '*')
			{
				p += 2;
				while (p + 1 < m_n && !(m_s[p] == '*' && m_s[p + 1] == '/'))
					p++;
				p = (p + 2 <= m_n) ? p + 2 : m_n;
			}
			else
			{
				break;
			}
		}
		return p;
	}

	bool ReadIdentifier(size_t& p, FString& out) const
	{
		p = SkipWhitespaceAndComments(p);
		if (p >= m_n || !IsIdentStart(m_s[p]))
			return false;

		size_t start = p;
		while (p < m_n && IsIdentChar(m_s[p]))
			p++;
		out = FString(m_s + start, p - start);
		return true;
	}

	bool Expect(size_t& p, char c) const
	{
		p = SkipWhitespaceAndComments(p);
		if (p >= m_n || m_s[p] != c)
			return false;
		p++;
		return true;
	}

	bool Peek(size_t p, char c) const
	{
		p = SkipWhitespaceAndComments(p);
		return p < m_n && m_s[p] == c;
	}

	// p is at the opening delimiter; returns the position after the matching closer.
	bool SkipBalanced(size_t& p, char open, char close) const
	{
		if (p >= m_n || m_s[p] != open)
			return false;

		int depth = 0;
		while (p < m_n)
		{
			if (m_s[p] == '/' && p + 1 < m_n && (m_s[p + 1] == '/' || m_s[p + 1] == '*'))
			{
				p = SkipWhitespaceAndComments(p);
				continue;
			}
			if (m_s[p] == open)
				depth++;
			else if (m_s[p] == close && --depth == 0)
			{
				p++;
				return true;
			}
			p++;
		}
		return false;
	}

	const char* Chars() const { return m_s; }

private:
	const char* m_s;
	size_t m_n;
};

void ParseLayout(const FString& layout, ProgramDecl& decl)
{
	decl.layout = layout;
	int index = layout.IndexOf("location");
	if (index >= 0)
	{
		const char* chars = layout.GetChars() + index + 8;
		while (*chars == ' ' || *chars == '\t') chars++;
		if (*chars == '=')
			decl.location = atoi(chars + 1);
	}
}

// Parses "[N]" / "[]" / "[EXPR]" at p (if present).
int ParseArraySuffix(const Scanner& scan, size_t& p)
{
	if (!scan.Peek(p, '['))
		return 0;

	p = scan.SkipWhitespaceAndComments(p);
	size_t start = p + 1;
	if (!scan.SkipBalanced(p, '[', ']'))
		return ProgramDecl::UnknownArraySize;

	FString expr(scan.Chars() + start, p - 1 - start);
	expr.StripLeftRight();
	if (expr.Len() == 0)
		return ProgramDecl::UnsizedArray;

	for (unsigned i = 0; i < expr.Len(); i++)
	{
		if (expr[i] < '0' || expr[i] > '9')
			return ProgramDecl::UnknownArraySize;
	}
	return atoi(expr.GetChars());
}

bool TryParseDeclaration(const Scanner& scan, size_t start, size_t& end, ProgramDecl& decl)
{
	size_t p = start;
	FString storage;
	FString interpolation, qualifiers, layout;

	while (true)
	{
		FString token;
		if (!scan.ReadIdentifier(p, token))
			return false;

		if (Eq(token, "layout"))
		{
			if (!scan.Peek(p, '('))
				return false;
			p = scan.SkipWhitespaceAndComments(p);
			size_t layoutStart = p + 1;
			if (!scan.SkipBalanced(p, '(', ')'))
				return false;
			layout = FString(scan.Chars() + layoutStart, p - 1 - layoutStart);
		}
		else if (Eq(token, "flat") || Eq(token, "noperspective") || Eq(token, "smooth"))
		{
			interpolation = token;
		}
		else if (Eq(token, "centroid") || Eq(token, "highp") || Eq(token, "mediump") || Eq(token, "lowp") || Eq(token, "invariant") || Eq(token, "precise"))
		{
			// Precision and centroid qualifiers carry no interface information the API needs.
		}
		else if (Eq(token, "readonly") || Eq(token, "writeonly") || Eq(token, "coherent") || Eq(token, "volatile") || Eq(token, "restrict"))
		{
			if (qualifiers.Len() > 0)
				qualifiers << " ";
			qualifiers << token;
		}
		else if (Eq(token, "uniform") || Eq(token, "in") || Eq(token, "out") || Eq(token, "buffer"))
		{
			storage = token;
			break;
		}
		else
		{
			return false;
		}
	}

	FString first;
	if (!scan.ReadIdentifier(p, first))
		return false;

	decl = ProgramDecl();
	decl.interpolation = interpolation;
	decl.qualifiers = qualifiers;
	ParseLayout(layout, decl);

	if ((Eq(storage, "uniform") || Eq(storage, "buffer")) && scan.Peek(p, '{'))
	{
		// Interface block: [layout] uniform|buffer BlockName { members } [instance][array];
		p = scan.SkipWhitespaceAndComments(p);
		size_t bodyStart = p;
		if (!scan.SkipBalanced(p, '{', '}'))
			return false;

		decl.kind = Eq(storage, "uniform") ? ProgramDecl::Kind::UniformBlock : ProgramDecl::Kind::StorageBuffer;
		decl.type = first;
		decl.blockBody = FString(scan.Chars() + bodyStart, p - bodyStart);

		FString instance;
		size_t q = p;
		if (scan.ReadIdentifier(q, instance))
		{
			decl.name = instance;
			p = q;
			decl.arrayCount = ParseArraySuffix(scan, p);
		}
	}
	else
	{
		decl.type = first;
		if (!scan.ReadIdentifier(p, decl.name))
			return false;

		decl.arrayCount = ParseArraySuffix(scan, p);

		if (Eq(storage, "in"))
			decl.kind = ProgramDecl::Kind::In;
		else if (Eq(storage, "out"))
			decl.kind = ProgramDecl::Kind::Out;
		else if (IsSamplerType(first))
			decl.kind = ProgramDecl::Kind::Sampler;
		else if (Eq(storage, "uniform"))
			decl.kind = ProgramDecl::Kind::Uniform;
		else
			return false;
	}

	if (!scan.Expect(p, ';'))
		return false;

	end = p;
	return true;
}

}

ParsedSource ParseProgramSource(const FString& source)
{
	ParsedSource result;
	const char* s = source.GetChars();
	size_t n = source.Len();
	Scanner scan(s, n);

		size_t i = 0;
	int depth = 0;
	bool statementStart = true;
	bool lineStart = true;

	auto appendRange = [&](size_t from, size_t to) {
		result.source.AppendCStrPart(s + from, to - from);
	};

	while (i < n)
	{
		char c = s[i];

		if (c == '/' && i + 1 < n && s[i + 1] == '/')
		{
			size_t from = i;
			while (i < n && s[i] != '\n')
				i++;
			appendRange(from, i);
			continue;
		}

		if (c == '/' && i + 1 < n && s[i + 1] == '*')
		{
			size_t from = i;
			i += 2;
			while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/'))
				i++;
			i = (i + 2 <= n) ? i + 2 : n;
			appendRange(from, i);
			lineStart = false;
			continue;
		}

		if (c == '\n')
		{
			result.source.AppendCStrPart("\n", 1);
			i++;
			lineStart = true;
			continue;
		}

		if (IsSpace(c))
		{
			result.source.AppendCStrPart(&c, 1);
			i++;
			continue;
		}

		if (c == '#' && lineStart)
		{
			// Preprocessor directive, including line continuations.
			size_t from = i;
			while (i < n && !(s[i] == '\n' && (i == from || s[i - 1] != '\\')))
				i++;
			bool isVersion = (i - from) >= 8 && strncmp(s + from, "#version", 8) == 0;
			if (!isVersion)
				appendRange(from, i);
			statementStart = true;
			continue;
		}

		lineStart = false;

		if (depth == 0 && statementStart)
		{
			size_t end;
			ProgramDecl decl;
			if (TryParseDeclaration(scan, i, end, decl))
			{
				result.decls.Push(decl);
				// keep the line count of the original source
				for (size_t k = i; k < end; k++)
				{
					if (s[k] == '\n')
						result.source.AppendCStrPart("\n", 1);
				}
				i = end;
				statementStart = true;
				continue;
			}
		}

		result.source.AppendCStrPart(&c, 1);
		i++;

		if (c == '{')
		{
			depth++;
			statementStart = false;
		}
		else if (c == '}')
		{
			if (depth > 0)
				depth--;
			statementStart = depth == 0;
		}
		else if (c == ';')
		{
			statementStart = depth == 0;
		}
		else
		{
			statementStart = false;
		}
	}

	return result;
}

bool Std140TypeLayout(const FString& type, int& alignment, int& size)
{
	struct Entry { const char* name; int alignment; int size; };
	static const Entry entries[] =
	{
		{ "bool", 4, 4 }, { "int", 4, 4 }, { "uint", 4, 4 }, { "float", 4, 4 },
		{ "bvec2", 8, 8 }, { "ivec2", 8, 8 }, { "uvec2", 8, 8 }, { "vec2", 8, 8 },
		{ "bvec3", 16, 12 }, { "ivec3", 16, 12 }, { "uvec3", 16, 12 }, { "vec3", 16, 12 },
		{ "bvec4", 16, 16 }, { "ivec4", 16, 16 }, { "uvec4", 16, 16 }, { "vec4", 16, 16 },
		{ "mat2", 16, 32 }, { "mat3", 16, 48 }, { "mat4", 16, 64 },
	};

	for (const Entry& entry : entries)
	{
		if (Eq(type, entry.name))
		{
			alignment = entry.alignment;
			size = entry.size;
			return true;
		}
	}
	return false;
}

bool ParseBlockMembers(const FString& body, TArray<BlockMember>& members)
{
	const char* s = body.GetChars();
	size_t n = body.Len();
	Scanner scan(s, n);

	size_t p = scan.SkipWhitespaceAndComments(0);
	if (p >= n || s[p] != '{')
		return false;
	p++;

	while (true)
	{
		p = scan.SkipWhitespaceAndComments(p);
		if (p >= n)
			return false;
		if (s[p] == '}')
			return true;

		FString type;
		while (true)
		{
			if (!scan.ReadIdentifier(p, type))
				return false;
			if (Eq(type, "highp") || Eq(type, "mediump") || Eq(type, "lowp") || Eq(type, "flat") || Eq(type, "noperspective") || Eq(type, "smooth") || Eq(type, "readonly") || Eq(type, "writeonly"))
				continue;
			break;
		}

		BlockMember member;
		member.type = type;
		if (!scan.ReadIdentifier(p, member.name))
			return false;
		member.arrayCount = ParseArraySuffix(scan, p);
		if (!scan.Expect(p, ';'))
			return false;
		members.Push(member);
	}
}

}
