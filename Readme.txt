This is the full source-code for the uz1/uz2/uz3 compression and decompression library, which
I created for my UnrealDeps-tool (http://www.unrealadmin.org/forums/showthread.php?p=160299)
(version: 0.1.0).

I release this source as I were only able to find the source for the uz2-algorithm in the web. All other
sources for UT-compression-tools employed ucc.exe, i.e. required a local installation of the games.
I hope this helps anybody.

I don't care what you do with the code; I would just appreciate it if you gave me credit in case you
use it in some of your projects.

Created by Gugi, 2010-2011
Support: http://www.unrealadmin.org/forums/showthread.php?p=160299





Total line count (of own code): 3076

Although C++/CLI is used for the uz2 and uz3 parts, it shouldn't be hard to port it to standard C++.
The uz1-part is already written in normal C++, but is a bit "messed up":
	Firstly I tried 4 different burrows-wheeler-approaces to find the fastest one. Thats the reason for
		the BWT_SORT_TYPE-define (the fastest is BWT_EXT_SORT) (in uz1Impl.h).
	Secondly in order to optimize the overall speed I tried to bypass the STL-streams and work with the
		buffers directly. This can be toggled on/off with the AGRESSIVE_OPTIMIZATION-define (in uz1Impl.cpp).
		
Used compiler:
	I used VS2008, SP1
	All compiler-optimizations turned on:
		Optimization: Maximize Speed
		Inline Function Expansion: Any Suitable
		Enable Intrinsic Functions: Yes
		Favor Size of Speed: Favor Fast Code

Source of the algorithms:
	- uz1: 
		Public UT99-headers: FCodec.h (all algorithms), USetupDefinition.cpp (function ProcessCopy() for the order)
		UT-Package-delphi-library: http://sourceforge.net/projects/utpackages/ (e.g. the compact-indices algorithm)
	- uz2: TinyUZ2: http://downloads.unrealadmin.org/UT2004/Tools/TinyUZ2/
	- uz3: This one I found out myself. Wasn't that hard, as the major difference to the uz2 one is that the complete file
			is being compressed at once. All I had to do is compare a uz2 and uz3 compressed file (both are similar at the
			beginning).

Files:
	- uzLib.h: Contains the managed classes (mostly only their interfaces).
			Language: C++/CLI
	- uzLib.cpp: Contains the implementation of the classes in uzLib.h
			Language: C++/CLI
	- uz1Impl.h: Contains the definitions of the classes required for the uz1-algorithm.
			Language: C++
	- uz1Impl.cpp: Contains the implementation of the uz1-classes.
			Language: C++

External dependencies:
	- zlib1.dll: Compiled zLib; required for uz2 and uz3
	- zlib.h: For the error-codes
	- bwtsort.h, bwtsort.c: http://sourceforge.net/projects/bwtcoder/files/bwtcoder/preliminary-2/
		Used to speed up the uz1-compression a lot
	- boost/dynamic_bitset.hpp: Used in the uz1 algorithm (in the Huffman-step)

