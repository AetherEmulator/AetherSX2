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

#include "PrecompiledHeader.h"
#include "GSShaderOGL.h"
#include "GLState.h"

#if 0

GSShaderOGL::GSShaderOGL(bool debug)
	: m_pipeline(0)
	, m_debug_shader(true)
{
}

GSShaderOGL::~GSShaderOGL()
{
	printf("Delete %zu Shaders, %zu Programs\n", m_shad_to_delete.size(), m_program.size());

	for (auto s : m_shad_to_delete)
		glDeleteShader(s);
	for (auto p : m_program)
		glDeleteProgram(p.second);
}

GLuint GSShaderOGL::LinkProgram(const char* pretty_name, GLuint vs, GLuint gs, GLuint ps)
{
	const ProgramShaders key{vs, gs, ps};
	auto it = m_program.find(key);
	if (it != m_program.end())
		return it->second;

	GLuint p = glCreateProgram();
	if (vs)
		glAttachShader(p, vs);
	if (ps)
		glAttachShader(p, ps);
	if (gs)
		glAttachShader(p, gs);

	glLinkProgram(p);

	ValidateProgram(p);

#ifdef _DEBUG
	if (pretty_name)
		glObjectLabel(GL_PROGRAM, p, std::strlen(pretty_name), pretty_print);
#endif

	m_program.emplace(key, p);

	return p;
}

void GSShaderOGL::BindProgram(GLuint vs, GLuint gs, GLuint ps)
{
	GLuint p = LinkProgram(nullptr, vs, gs, ps);
	glUseProgram(p);
}

void GSShaderOGL::BindProgram(GLuint p)
{
	glUseProgram(p);
}

bool GSShaderOGL::ValidateShader(GLuint s)
{
	if (!m_debug_shader)
		return true;

	GLint status = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &status);
	if (status)
		return true;

	GLint log_length = 0;
	glGetShaderiv(s, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0)
	{
		char* log = new char[log_length];
		glGetShaderInfoLog(s, log_length, NULL, log);
		Console.Error("Shader compile failed: %s", log);
		delete[] log;
	}

	return false;
}

bool GSShaderOGL::ValidateProgram(GLuint p)
{
	if (!m_debug_shader)
		return true;

	GLint status = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &status);
	if (status)
		return true;

	GLint log_length = 0;
	glGetProgramiv(p, GL_INFO_LOG_LENGTH, &log_length);
	if (log_length > 0)
	{
		char* log = new char[log_length];
		glGetProgramInfoLog(p, log_length, NULL, log);
		Console.Error("Program link failed: %s", log);
		delete[] log;
	}

	return false;
}

std::string GSShaderOGL::GenGlslHeader(const std::string& entry, GLenum type, const std::string& macro)
{
	std::string header;

	if (GLLoader::is_gles)
	{
		if (GLAD_GL_ES_VERSION_3_2)
			header = "#version 320 es\n";
		else if (GLAD_GL_ES_VERSION_3_1)
			header = "#version 310 es\n";

		if (GLAD_GL_EXT_blend_func_extended)
			header += "#extension GL_EXT_blend_func_extended : require\n";
		if (GLAD_GL_ARB_blend_func_extended)
			header += "#extension GL_ARB_blend_func_extended : require\n";

		if (GLLoader::found_framebuffer_fetch)
		{
			if (GLAD_GL_ARM_shader_framebuffer_fetch)
				header += "#extension GL_ARM_shader_framebuffer_fetch : require\n";
			else if (GLAD_GL_EXT_shader_framebuffer_fetch)
				header += "#extension GL_EXT_shader_framebuffer_fetch : require\n";
		}

		header += "precision highp float;\n";
		header += "precision highp int;\n";
		header += "precision highp sampler2D;\n";
		if (GLAD_GL_ES_VERSION_3_1)
			header += "precision highp sampler2DMS;\n";
		if (GLAD_GL_ES_VERSION_3_2)
			header += "precision highp usamplerBuffer;\n";

		if (!GLAD_GL_EXT_blend_func_extended && !GLAD_GL_ARB_blend_func_extended)
		{
			if (!GLAD_GL_ARM_shader_framebuffer_fetch)
				fprintf(stderr, "Dual source blending is not supported\n");

			header += "#define DISABLE_DUAL_SOURCE\n";
		}

		if (GLLoader::found_framebuffer_fetch)
			header += "#define HAS_FRAMEBUFFER_FETCH 1\n";
		else
			header += "#define HAS_FRAMEBUFFER_FETCH 0\n";
	}
	else
	{
		header = "#version 330 core\n";

		// Need GL version 420
		header += "#extension GL_ARB_shading_language_420pack: require\n";
		// Need GL version 410
		header += "#extension GL_ARB_separate_shader_objects: require\n";
		if (GLLoader::found_GL_ARB_shader_image_load_store)
		{
			// Need GL version 420
			header += "#extension GL_ARB_shader_image_load_store: require\n";
		}
		else
		{
			header += "#define DISABLE_GL42_image\n";
		}

		header += "#define HAS_FRAMEBUFFER_FETCH 0\n";
	}

	if (GLLoader::has_clip_control)
		header += "#define HAS_CLIP_CONTROL 1\n";
	else
		header += "#define HAS_CLIP_CONTROL 0\n";

	if (GLLoader::vendor_id_amd || GLLoader::vendor_id_intel)
		header += "#define BROKEN_DRIVER as_usual\n";

	// Stupid GL implementation (can't use GL_ES)
	// AMD/nvidia define it to 0
	// intel window don't define it
	// intel linux refuse to define it
	if (GLLoader::is_gles)
		header += "#define pGL_ES 1\n";
	else
		header += "#define pGL_ES 0\n";

	// Allow to puts several shader in 1 files
	switch (type)
	{
		case GL_VERTEX_SHADER:
			header += "#define VERTEX_SHADER 1\n";
			break;
		case GL_GEOMETRY_SHADER:
			header += "#define GEOMETRY_SHADER 1\n";
			break;
		case GL_FRAGMENT_SHADER:
			header += "#define FRAGMENT_SHADER 1\n";
			break;
		default:
			ASSERT(0);
	}

	// Select the entry point ie the main function
	header += format("#define %s main\n", entry.c_str());

	header += macro;

	return header;
}

// Same as above but for not-separated build
GLuint GSShaderOGL::CompileShader(const char* glsl_file, const std::string& entry, GLenum type, const std::string& common_header, const char* glsl_h_code, const std::string& macro_sel /* = "" */)
{
	ASSERT(glsl_h_code != NULL);

	GLuint shader = 0;

	// Note it is better to separate header and source file to have the good line number
	// in the glsl compiler report
	const int shader_nb = 3;
	const char* sources[shader_nb];

	std::string header = GenGlslHeader(entry, type, macro_sel);

	sources[0] = header.c_str();
	sources[1] = common_header.data();
	sources[2] = glsl_h_code;

	shader = glCreateShader(type);
	glShaderSource(shader, shader_nb, sources, NULL);
	glCompileShader(shader);

	bool status = ValidateShader(shader);

	if (!status)
	{
		// print extra info
		Console.Error("Failed to compile shader:");
		Console.Error("%s (entry %s, prog %d) :", glsl_file, entry.c_str(), shader);
		Console.Error("%s", macro_sel.c_str());
	}

	m_shad_to_delete.push_back(shader);

	return shader;
}

#endif