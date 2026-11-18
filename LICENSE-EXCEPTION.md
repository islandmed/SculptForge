# SculptForge Scripting Exception, version 1.0

SculptForge is licensed under the GNU General Public License, version 3 or (at your option) any later version
(LICENSE), and carries the SculptForge Scripting Exception 1.0 below: programs, scripts and tools that use SculptForge
only through its public Python API, command line, MCP server or plug-in SDK may be under any licence.

This is an additional permission under section 7 of the GNU General Public License, version 3 (the "GPLv3"). It
applies to every version of SculptForge whose licence notice says that it carries this exception.

## 1. Definitions

**"SculptForge"** means the program distributed under the GPLv3 with a notice that it carries this exception.

**"Public Interfaces"** means:

- (a) the Python API: the `sculptforge` module and its documented functions, classes and properties;
- (b) the command line: `sculptforge-cli` and `sculptforge`, their documented options, commands and script syntax;
- (c) the Model Context Protocol (MCP) server that `sculptforge-cli --mcp` provides;
- (d) the documented plug-in SDK (`sdk/plugin`): its header and interface files and the calls they describe.

**"Independent Work"** means a program, script, add-on, plug-in or tool that interacts with SculptForge only through
the Public Interfaces and contains no part of SculptForge, modified or not, except:

- (i) the names, declarations and interface files needed to use the Public Interfaces (for example the plug-in
  SDK's header files), and
- (ii) code taken from the examples in SculptForge's documentation or its `examples` folder.

## 2. The permission

You may make, run, copy, modify and distribute an Independent Work under terms of your choice, consistent with the
licences of any other code it contains. An Independent Work is not a work based on SculptForge merely because it
uses the Public Interfaces, even when it runs in the same process as SculptForge (for example after
`import sculptforge`) or loads SculptForge as a library.

## 3. What stays under the GPLv3

This exception does not change the licence of SculptForge itself. SculptForge, any modified version of it, its
bindings and interfaces (the Python module, the command-line programs, the MCP server, the plug-in runtime), and
any code copied from it other than as allowed in section 1 remain under the GPLv3. When you convey them, including
bundled with an Independent Work, the GPLv3 applies to them, including its requirement to offer their source code.
The Independent Work alongside them stays under your terms.

## 4. Modified versions

If you modify SculptForge, you may extend this exception to your version, but you are not required to. If you do
not, delete the notice that it carries this exception from your version.

## 5. Nothing else changes

Apart from the permission above, the GPLv3 applies unchanged. This exception grants no right to use the
SculptForge name, logo or other marks.
