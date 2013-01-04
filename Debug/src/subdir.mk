################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/cached.c \
../src/download_worker.c \
../src/downloader.c \
../src/fparser.c 

OBJS += \
./src/cached.o \
./src/download_worker.o \
./src/downloader.o \
./src/fparser.o 

C_DEPS += \
./src/cached.d \
./src/download_worker.d \
./src/downloader.d \
./src/fparser.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I/usr/include/mysql/ -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


