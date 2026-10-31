using System;
using System.IO;
using System.Reflection;
using System.Text;
using System.Collections.Generic;
using System.Diagnostics;
using System.Web.Script.Serialization;
using System.Windows.Media.Imaging;

namespace Kharvox.Circle {
static class EmbeddedResources {
    public static byte[] Bytes(string name) {
        using(var source=Assembly.GetExecutingAssembly().GetManifestResourceStream("Circle."+name)) {
            if(source==null)throw new InvalidDataException("Missing embedded resource: "+name);
            using(var data=new MemoryStream()){source.CopyTo(data);return data.ToArray();}
        }
    }
    public static string Text(string name) {
        using(var reader=new StreamReader(new MemoryStream(Bytes(name)),Encoding.UTF8,true))return reader.ReadToEnd();
    }
    public static BitmapImage Image(string name) {
        using(var stream=new MemoryStream(Bytes(name))) {
            var image=new BitmapImage();image.BeginInit();image.CacheOption=BitmapCacheOption.OnLoad;
            image.StreamSource=stream;image.EndInit();image.Freeze();return image;
        }
    }
}

// The established launch policy is embedded and sent through stdin. Nothing is
// extracted to disk, and user paths are data in JSON, never PowerShell source.
static class EmbeddedLaunch {
    public static string PayloadJson(string json){return Payload(new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(json));}
    static string Data(string value){return Convert.ToBase64String(Encoding.UTF8.GetBytes(value));}
    static string Decode(string value){return "[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"+Data(value)+"'))";}
    public static string Payload(IDictionary<string,object> arguments) {
        var code=new StringBuilder("$ErrorActionPreference='Stop'\n");
        code.AppendLine("Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Force");
        code.AppendLine(EmbeddedResources.Text("settings-path.ps1"));
        code.AppendLine(EmbeddedResources.Text("vr-intro.ps1"));
        code.AppendLine("function Install-CircleCommunityFramegen {\n"+EmbeddedResources.Text("install-community-framegen.ps1")+"\n}");
        code.AppendLine("function Invoke-CircleXrDiagnostic {\n"+EmbeddedResources.Text("test-xr.ps1")+"\n}");
        code.AppendLine("function Start-CircleGame {\n"+EmbeddedResources.Text("launch.ps1")+"\n}");
        code.AppendLine("$script:CircleEmbeddedBuildCatalog="+Decode(EmbeddedResources.Text("motor-builds.json")));
        code.AppendLine("$values="+Decode(new JavaScriptSerializer().Serialize(arguments))+" | ConvertFrom-Json");
        code.AppendLine("$launchArgs=@{};foreach($p in $values.PSObject.Properties){$launchArgs[$p.Name]=$p.Value}");
        code.AppendLine("try { Start-CircleGame @launchArgs } catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }");
        return code.ToString();
    }
    public static ProcessStartInfo StartInfo(string root) {
        var bootstrap="[Console]::InputEncoding=[Text.Encoding]::UTF8; $source=[Console]::In.ReadToEnd(); & ([scriptblock]::Create($source))";
        var info=new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"WindowsPowerShell","v1.0","powershell.exe"),
            "-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand "+Convert.ToBase64String(Encoding.Unicode.GetBytes(bootstrap))) {
            WorkingDirectory=root,UseShellExecute=false,CreateNoWindow=true,
            RedirectStandardInput=true,RedirectStandardOutput=true,RedirectStandardError=true
        };
        info.EnvironmentVariables.Remove("PSModulePath");
        return info;
    }
}
}
