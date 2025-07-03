# This source file is part of RmlUi, the HTML/CSS Interface Middleware
# 
# For the latest information, see http://github.com/mikke89/RmlUi
# 
# Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors
# Copyright (c) 2019-2023 The RmlUi Team, and contributors
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import os
import subprocess

# Compiles all shader_*.hlsl files in this directory to SPIR-V binary C character arrays. Requires 'fxc' installed and available system-wide.

out_file = "ShadersCompiled.h"

current_dir = os.path.dirname(os.path.realpath(__file__));

temp_spirv_path = os.path.join(current_dir, ".temp.fxc")
out_path = os.path.join(current_dir, out_file)
debug_out_path = os.path.join(current_dir, "debug")

if not os.path.exists(debug_out_path):
	os.mkdir(debug_out_path)

with open(out_path, "w") as result_file:
	result_file.write("// RmlUi DirectX 11 shaders compiled using command: 'python compile_shaders.py'. Do not edit manually.\n\n#include <stdint.h>\n")

	for file in os.listdir(current_dir):
		if file.startswith("shader_") and file.endswith(".hlsl"):
			file_name = os.path.splitext(file)[0]
			shader_path = os.path.join(current_dir, file)
			shader_debug_path = os.path.join(debug_out_path, file_name)
			shader_profile = file_name.split('_')[1]

			match shader_profile:
				case "pixel": profile = "ps_5_0"
				case "vertex": profile = "vs_5_0"
				case _: # TODO: error
					continue

			print("Compiling '{}' using fxc.".format(file))

			subprocess.run([
				r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.20348.0\x64\fxc.exe",
				"/nologo",
				"/T", profile,
				"/Fo", temp_spirv_path,
				# debug parameters
				"/Zi", # debug information
				"/Od", # disable optimizations
				"/Fd", shader_debug_path + ".pdb", # write pdb
				"/Fc", shader_debug_path + ".asm.hlsl", # write assembly
				shader_path
			], check = True)

			print("Success, writing output to variable '{}' in {}".format(file_name, out_file))

			i = 0
			result_file.write("\nalignas(uint32_t) static const uint8_t {}[] = {{".format(file_name))
			for b in open(temp_spirv_path, "rb").read():
				if i % 20 == 0:
					result_file.write("\n\t")
				result_file.write("0x%02X," % b)
				i += 1
				
			result_file.write("\n};\n")
			
			os.remove(temp_spirv_path)
