# CH32V203DE
ch32v203在linux下编译和开发的环境。程序在ch32v203apps文件夹下。

## 依赖
需要去[wch官网](http://www.mounriver.com/download)下载linux环境编译器（文件名MRS_Toolchain_Linux_x64_V210.tar.xz，如果有新版本也可以使用），并设置WCH_TOOLCHAIN_PATH环境变量指向对应的编译器路径，如'export WCH_TOOLCHAIN_PATH=~/wchgcc/bin/'，或在.mk文件中编辑PATH_TO_TOOLCHAIN=指定编译器的路径。

## 编译与下载
可用'make -j'编译，'make -j flash'下载，需要使用minichlink，编译生成的.bin和.hex文件在例程目录的obj目录下。

