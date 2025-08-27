#include "uz1Impl.h"

#include <iostream>
using namespace std;

int main(int argc, char* argv[]) {
    basic_ifstream<Bytef> InputStream;
    basic_ofstream<Bytef> OutputStream;

    if(argc != 3) {
        cout << "Usage: " << argv[0] << " <input> <output>" << endl;
        return 1;
    }
    string src_fn(argv[1]);
    string dst_fn(argv[2]);

    InputStream.open(src_fn.c_str(), ios_base::in | ios_base::binary);
    if (!InputStream.is_open() || !InputStream.good() || InputStream.eof()) {
        cout << "Couldn't open input file '" + src_fn + "'." << endl;
        return 1;
    }

    OutputStream.open(dst_fn.c_str(), ios_base::out | ios_base::trunc | ios_base::binary);
    if (!OutputStream.is_open() || !OutputStream.good()) {
        cout << "Couldn't open output file '" + dst_fn + "'." << endl;
        return 1;
    }
    InputStream.exceptions(std::ios::failbit | std::ios::badbit);
    OutputStream.exceptions(std::ios::failbit | std::ios::badbit);

    uzLib::DecompressFromUz1(InputStream, OutputStream, NULL, NULL);
    InputStream.close();
    OutputStream.close();

    return 0;
}
