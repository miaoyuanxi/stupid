# create some soft links for the shared object files

cd ../../gnu/zlib/lib/linux

ln -s libz.so.1.2.8 libz.so.1
ln -s libz.so.1.2.8 libz.so

cd -
cd ../../gnu/openssl/lib/linux

ln -s libcrypto.so.1.0.0 libcrypto.so
ln -s libssl.so.1.0.0 libssl.so

cd -
cd ../../gnu/curl/lib/linux

ln -s libcurl.so.4.3.0 libcurl.so.4
ln -s libcurl.so.4.3.0 libcurl.so