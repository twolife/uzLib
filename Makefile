all: uzlib-cli

uzlib-cli: uz1Impl.cpp cli.c
	$(CXX) uz1Impl.cpp cli.c -o uzlib-cli
