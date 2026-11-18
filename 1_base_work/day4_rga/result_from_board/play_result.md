
```shell
ffmpeg -v error -i result_from_board/main_1080p.h264 -f null - && echo "main ok"

```
```shell
ffmpeg -v error -i result_from_board/sub_720p.h264  -f null - && echo "sub ok"

```



```shell
ffplay -i result_from_board/main_1080p.h264

```
```shell
ffplay -i result_from_board/sub_720p.h264
```