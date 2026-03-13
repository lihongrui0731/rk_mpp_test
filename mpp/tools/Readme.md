rm -rf *.yuv
rm -rf *.h264
//tools 
//解码Jpg 到YUV
./mpi_dec_test -i test.jpg -o test.yuv -c jpeg -w 4000 -h 3000 -f yuv420p
//编码YUV 到H264
./mpi_enc_test -i test.yuv -o test.h264 -c h264 -w 4000 -h 3000 -f nv12 -fps 1 -g 1 -b 2000000