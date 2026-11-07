param([string]$Path,[int]$Bytes=4000)
$fs=[System.IO.File]::Open($Path,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite)
$len=$fs.Length
$start=[Math]::Max(0,$len-$Bytes)
$fs.Seek($start,[System.IO.SeekOrigin]::Begin) | Out-Null
$buf=New-Object byte[] $Bytes
$n=$fs.Read($buf,0,$Bytes)
$fs.Close()
Write-Output ("LEN=" + $len)
[System.Text.Encoding]::UTF8.GetString($buf,0,$n)
