/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021 PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#if 0

#include "GS.h"
#include <functional>

class GSShaderOGL
{
	struct ProgramShaders
	{
		GLuint vs;
		GLuint gs;
		GLuint ps;

		__fi bool operator==(const ProgramShaders& s) const { return vs == s.vs && gs == s.gs && ps == s.ps; }
		__fi bool operator!=(const ProgramShaders& s) const { return vs != s.vs && gs != s.gs && ps != s.ps; }
		__fi bool operator<(const ProgramShaders& s) const { return vs < s.vs || gs < s.gs || ps < s.ps; }
	};

	struct ProgramPipelineHash
	{
		template <typename T, typename... Rest>
		static __fi void hash_combine(std::size_t& seed, const T& v, const Rest&... rest)
		{
			seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			(hash_combine(seed, rest), ...);
		}

		__fi std::size_t operator()(const ProgramShaders& p) const noexcept
		{
			std::size_t h = 0;
			hash_combine(h, p.vs, p.gs, p.ps);
			return h;
		}
	};

	GLuint m_pipeline;
	std::unordered_map<ProgramShaders, GLuint, ProgramPipelineHash> m_program;
	const bool m_debug_shader;

	std::vector<GLuint> m_shad_to_delete;
	std::vector<GLuint> m_prog_to_delete;

	bool ValidateShader(GLuint s);
	bool ValidateProgram(GLuint p);

	std::string GenGlslHeader(const std::string& entry, GLenum type, const std::string& macro);

public:
	GSShaderOGL(bool debug);
	~GSShaderOGL();

	// Same as above but for not separated build
	void BindProgram(GLuint vs, GLuint gs, GLuint ps);
	void BindProgram(GLuint p);

	GLuint CompileShader(const char* glsl_file, const std::string& entry, GLenum type, const std::string& common_header, const char* glsl_h_code, const std::string& macro_sel = "");
	GLuint LinkProgram(const char* pretty_name, GLuint vs, GLuint gs, GLuint ps);
};
#endif