
```shell
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 -framerate 30 result_from_board/bbb.yuv
```

```shell
ffplay -f rawvideo -pixel_format nv12 -video_size 1080x1920 -framerate 30 result_from_board/bbb.yuv
```



```shell
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 -framerate 30 result_from_board/out.nv12
```