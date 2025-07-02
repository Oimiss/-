
include config.mk
all:
#-C是指定目录
#make -C signal   

#可执行文件应该放最后
#make -C app      

#用shell命令for搞，shell里边的变量用两个$
	@for %%d in ($(BUILD_DIR)) do make -C %%d


clean:
#-rf：删除文件夹，强制删除
	rd /s /q app\link_obj
	rd /s /q app\dep
	del /f /q nginx.exe
	del /f /q signal\*.gch app\*.gch

