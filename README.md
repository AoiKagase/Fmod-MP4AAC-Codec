# Fmod-MP4AAC-Codec
FMODライブラリを利用してiTunes製M4A/AACを再生するコーデックです。<br>
Codec to play iTunes MP4/AAC using FMOD library.<br>

タグの解析を無理やり行っているため、iTunes以外のM4A/AACではタグ解析に失敗する可能性があります。<br>
Because tag analysis is performed for iTunes, tag analysis may fail for non-iTunes M4A/AAC.<br>

利用に際してはFMODライブラリのドキュメントを参照してください。<br>
Please refer to the FMOD library documentation for details.<br>
<br>
C# Example.<br>
```C#
public void LoadPlugins()
{
	uint handle;
	PLUGINTYPE plugintype;
	uint version;

	FmodSystem.setPluginPath(".\\Plugins");
	FmodSystem.loadPlugin("codec_mp4.dll", out handle, 100);
//	FmodSystem.getPluginInfo(handle, out plugintype, out version);
	return;
}
```

TODO: Optimize.
