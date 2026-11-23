using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;

[assembly: AssemblyTitle("FF8 Reborn Installer")]
[assembly: AssemblyProduct("FF8 Reborn")]
[assembly: AssemblyVersion("1.4.8.0")]

internal static class Juego {
    internal const string Nombre = "FINAL FANTASY VIII Remastered";
    const string Original = "20179B6187E278F07B7373465D49D167A7E298F4BCA40C1B28D70D8D984191D9";
    const string Motor = "B0097E4EDD96FA2C32745B04328939EB8D182C2E89121D51405BC486B13F9108";
    const string Anterior = "FA571A5249A5A6775CCF896172C459EFD005A466B1AB2C30DDA93A3916268BDA";
    const string RebornInicial = "1849E91A6ABA02D17C36847DAFB837C35221A1BE3B5F2CFB12FAA91BA8235999";
    const string RebornFranjas = "E18338E81CB66598617D08ED46FCE069C136F4B5DE768F00DB0E5C58BE1E801C";
    const string RebornPanorama = "1EFC9A3FAD7FF2B7CF22814101B610DF6FFEBF2AF134B251438532D4DABAA869";
    const string RebornCosta = "A0B1EEBD23067EF532C7721966D03061A6862E950C64E33E958B7AAA5BD67B6B";
    const string RebornGiro = "0D71AF3F5DB98F365406EB26973C51607882DC863083BFAE8611A4B68014A07E";
    const string RebornCarrera = "47BDF156ACB38D67915941FEA23931CFCA345B522ACCAF54B15684F148A2ACBD";
    const string RebornCabeza = "E50D3B13DA2F81AFBAD1AB6F6ADC47149DAB744060510919E148B0423DACBA8F";
    const string RebornZancada = "52C41D3620E31FF73CE3A4DA1A84A39B1DA425F17B07F043F309289B5AAF756B";
    const string RebornPerfil = "8985198EE8935B3F5F677850FB6C902392505D2D4BB74E57D21E894ED688D546";
    const string RebornLateral = "6C884F577EDB0C9B0B4BDE92A33AB25F8CDB4C09570266427CB4A9695A669C53";
    const string RebornPaso = "DDDBAA0E8853697453A7C60003A88BA5ECADEA2CCB761324245A26DD3BD7769C";
    const string RebornSeguimiento = "1EEE2959FFB70CC95FC385490A1361957B554D398B6420C1E522C008AB8F5178";
    const string RebornEstabilidad = "3ECA943996D00BB92A233FA8105F8C681F0FF28559669D7068E079E485B384EB";
    const string RebornMapaProporcional = "116CFEB107EC4AEEDF023C83010B72017820197C5D462246773C105ABE70ABCD";
    const string RebornCapas = "8746AEA2F19C6A131A4EC9F040DD0DB981F1B566DE9547941F095382931091C4";
    const string RebornRitmo = "265CB0758AF076B81893E0C1DED6726FBF580B866173756E73C32FC4A37611DC";
    const string RebornAntesDeRetro = "B97A50C93A1CB3D58134FD1DD0815EB7FA8C4A5ED40762D5B24FDD6DA99F84B4";
    const string RebornPerfiles = "C32D346E68929875416678AC381E757FA4DA1112B7D3D80BE382435BA8055477";
    const string RebornPanel = "9DF1F2831834474F195442CD2308D51960F315AF4FA442CAC49D4B6C6A0747BF";
    const string RebornRetro = "E5942740A8C657DF4047138AA1E7A939F4845190BAD59F83571BB6AA63A23BC0";
    const string RebornAtlasEstable = "CD2C3DC71EA0EF091757E7D0A04132AF21A8CAB65C160E389D684E613B07F415";
    const string RebornEspecial = "5C257B631068B97D5BDB68784798889CB29E7C254F217387D9A87A65D8006B9F";
    const string RebornScroll = "4F0FAEE8B0A09D370088A8B533732945EA2B9209F058F79FAD4726471F73861D";
    const string RebornFuente = "63942B6B61AD75293DE05E69F5A9A9A58096FA1D5B95B63155F9A6415EF03004";
    const string RebornMenu = "17F0FCEAFD6C04040F2F9A8BD48794570D6C88E7489D09950457C9CD739C24E7";
    const string RebornTransicion = "549D92F6E1411BCF5C23FDB33035D6210D0EAC4D3E0FBE831525F8B37B9C0FEF";
    const string RebornRecomendada = "078362DB5DF9FA81E34F99E2069A0D16B38DC0C58ED3FA6EA2981195352FCAD5";
    const string RebornPS1 = "41F4388F3797820CFC5EC4FFC7962C96D5C9DF3595DDDEA9723B934671FDCF5B";
    const string RebornPanelVivo = "5D07F002AC2D73F7D7DDF653A387588C311A710E027E259863A267BD6D716D59";
    const string RebornCustomCopy = "3F5E6E59BB11D85FA59993BD66FBE79D6E420192129EF1A5A21632AB5DD74507";
    const string RebornEnglish = "42ED54AD00B57263C9108B2B42EC7E7E36CB67C470903A8AB487C068E76A035F";
    static bool EsAnterior(string firma) {
        if(firma=="A24E0D8E2D028908C27392649E11D00B61A48E70D81F346FE1FB848736F12A50")return true;
        if(firma=="93423D8D58C4BC4FCE546D4420646DBEC6C14D5F9B8DC1E42DCC7639FB41424C")return true;
        if(firma=="3439053ABE15D66CDBF5465DFD18E937F0ADB76A5C614E01EEBADC3D48D8091E")return true;
        if(firma=="CBCF42706FE76196AB1B512B78083CB853A35F2C0DF7ECFDF8F1FBEB5B98DFD5")return true;
        if(firma=="2B712AB4E4FD625B9FC93FA35B8A4E6EBAB4CEAC71AFEB9D7B6969291C7BA359")return true;
        if(firma=="5C6110649CACC9AF074A59B736B15508D06211B399ACA192C772D87F1373BA53")return true;
        if(firma=="11D07426C9C451A4AB43B0914EB1E743C0C299AFAE19C7C09F35E7FE051F4E15")return true;
        if(firma=="EF5E09362407D0A3BECD31FB5A6FF97D71360F120734250AA24622C92B97CE52")return true;
        if(firma=="3292AC8FD5924204644DDDEA45AD3967770B98FA81371AB509202A3CC3F3262D")return true;
        if(firma=="0B1D0584940091A82517C4A589ECCCCE099136B1A7066D3E8079763979C26478")return true;
        if(firma=="B94861D2BE712BA0AEDF0E46678F3622C8864078AAE8524623F71C9EDE0ED23A")return true;
        if(firma=="4850A85CC7C491132D14C4E36E679F1363917CF5EA65E39A3C47868F39A442C3")return true;
        const string publicV1="95B0B8CBBCD5436B7B7F734BCAC12AAEB66A0095C4C5F70E134E13721186E43D";
        const string setupPreview="F180DF0C89DAF6B6D98F6DBF6C29416B586F9B4A8AA2E811C0229E7290EE1EC5";
        if(firma==RebornEnglish||firma==publicV1||firma==setupPreview)return true;
        return firma==Anterior||firma==RebornInicial||firma==RebornFranjas||firma==RebornPanorama||firma==RebornCosta||firma==RebornGiro||firma==RebornCarrera||firma==RebornCabeza||firma==RebornZancada||firma==RebornPerfil||firma==RebornLateral||firma==RebornPaso||firma==RebornSeguimiento||firma==RebornEstabilidad||firma==RebornMapaProporcional||firma==RebornCapas||firma==RebornRitmo||firma==RebornAntesDeRetro||firma==RebornRetro||firma==RebornPanel||firma==RebornPerfiles||firma==RebornAtlasEstable||firma==RebornEspecial||firma==RebornScroll||firma==RebornFuente||firma==RebornMenu||firma==RebornTransicion||firma==RebornRecomendada||firma==RebornPS1||firma==RebornPanelVivo||firma==RebornCustomCopy;
    }
    static string Mod { get { return Paquete.Firma; } }
    internal static void VerificarInstalado(string ruta) {
        Exigir(VersionActual(ruta),"Install FF8 Reborn in the selected folder first.");
    }
    internal static bool VersionActual(string ruta) {
        return Directory.Exists(ruta) && Firma(Path.Combine(ruta,"libvgmstream.dll"))==Mod;
    }

    internal static string Detectar() {
        var raices = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var clave in new[]{@"HKEY_CURRENT_USER\Software\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", @"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam"}) {
            foreach(var nombre in new[]{"SteamPath", "InstallPath"}) {
                try {
                    var ruta = Registry.GetValue(clave,nombre,null) as string;
                    if(!String.IsNullOrWhiteSpace(ruta)) raices.Add(ruta);
                } catch(System.Security.SecurityException) {} catch(UnauthorizedAccessException) {}
            }
        }
        raices.Add(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam"));
        foreach(var disco in DriveInfo.GetDrives()) if(disco.DriveType==DriveType.Fixed)
            raices.Add(Path.Combine(disco.Name,"SteamLibrary"));
        foreach(var raiz in new List<string>(raices)) {
            var indice=Path.Combine(raiz,"steamapps","libraryfolders.vdf");
            if(!File.Exists(indice)) continue;
            try {
                foreach(Match m in Regex.Matches(File.ReadAllText(indice), @"""path""\s+""([^""]+)"""))
                    raices.Add(m.Groups[1].Value.Replace(@"\\",@"\"));
            } catch(IOException) {} catch(UnauthorizedAccessException) {}
        }
        foreach(var raiz in raices) {
            var apps=Path.Combine(raiz,"steamapps");
            var manifest=Path.Combine(apps,"appmanifest_1026680.acf");
            var nombre=Nombre;
            if(File.Exists(manifest)) try {
                var m=Regex.Match(File.ReadAllText(manifest),@"""installdir""\s+""([^""]+)""");
                if(m.Success && Path.GetFileName(m.Groups[1].Value)==m.Groups[1].Value) nombre=m.Groups[1].Value;
            } catch(IOException) {} catch(UnauthorizedAccessException) {}
            var ruta=Path.Combine(apps,"common",nombre);
            if(File.Exists(Path.Combine(ruta,"FFVIII_EFIGS.dll"))) return Path.GetFullPath(ruta);
        }
        return "";
    }
    internal const string GogId="1086370078";
    internal static bool EsGog(string ruta) {
        return !String.IsNullOrWhiteSpace(ruta) && File.Exists(Path.Combine(ruta,"goggame-"+GogId+".info"));
    }
    internal static string DetectarGog() {
        foreach(var raiz in new[]{@"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node",@"HKEY_LOCAL_MACHINE\SOFTWARE",@"HKEY_CURRENT_USER\SOFTWARE"}){
            try {
                var ruta=Registry.GetValue(raiz+@"\GOG.com\Games\"+GogId,"path",null) as string;
                if(EsGog(ruta))return ruta;
            } catch(System.Security.SecurityException) {} catch(UnauthorizedAccessException) {}
        }
        foreach(var disco in DriveInfo.GetDrives()) if(disco.DriveType==DriveType.Fixed){
            var ruta=Path.Combine(disco.Name,"GOG Games","FINAL FANTASY VIII - REMASTERED");
            if(EsGog(ruta))return ruta;
        }
        return "";
    }
    static string Firma(string archivo) {
        if(!File.Exists(archivo)) return "";
        using(var f=File.OpenRead(archivo)) using(var sha=SHA256.Create())
            return BitConverter.ToString(sha.ComputeHash(f)).Replace("-","");
    }
    static void Exigir(bool ok,string motivo) { if(!ok) throw new InvalidOperationException(motivo); }
    // Both installers use the same receipt as the renderer. A matching file
    // name alone does not establish ownership of a DLL.
    static string LimpiarActualizaciones(string ruta,string mensaje) {
        string registro=Path.Combine(ruta,"FF8-Reborn-update.txt");
        try {
            var info=new FileInfo(registro);
            if(!info.Exists || (info.Attributes&FileAttributes.ReparsePoint)!=0 || info.Length>256)return mensaje;
            Match m=Regex.Match(File.ReadAllText(registro),
                @"\AFF8-Reborn-Update-1\nwindows\n([AB])\n([1-9][0-9]{0,9})\n([a-f0-9]{64}|-)\n([a-f0-9]{64}|-)\n\z");
            uint build;
            if(!m.Success || !UInt32.TryParse(m.Groups[2].Value,out build) ||
                m.Groups[m.Groups[1].Value=="A"?3:4].Value=="-")return mensaje;
            for(int i=0;i<2;i++){
                string firma=m.Groups[3+i].Value;
                string archivo=Path.Combine(ruta,i==0?"FF8-Reborn-A.dll":"FF8-Reborn-B.dll");
                if(firma!="-" && File.Exists(archivo) && (File.GetAttributes(archivo)&FileAttributes.ReparsePoint)==0 &&
                   String.Equals(Firma(archivo),firma,StringComparison.OrdinalIgnoreCase))File.Delete(archivo);
            }
            File.Delete(registro);
        } catch(UnauthorizedAccessException) {return mensaje+" Could not remove all cached update files.";}
          catch(IOException) {return mensaje+" Could not remove all cached update files.";}
        return mensaje;
    }
    [System.Runtime.InteropServices.DllImport("kernel32.dll",CharSet=System.Runtime.InteropServices.CharSet.Unicode,SetLastError=true)]
    static extern uint GetFinalPathNameByHandle(Microsoft.Win32.SafeHandles.SafeFileHandle archivo,
        System.Text.StringBuilder ruta,uint capacidad,uint flags);
    static string RutaMotor(string carpeta) {
        using(var archivo=new FileStream(Path.Combine(carpeta,"FFVIII_EFIGS.dll"),FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)) {
            var ruta=new System.Text.StringBuilder(512);
            uint escritos=GetFinalPathNameByHandle(archivo.SafeFileHandle,ruta,(uint)ruta.Capacity,0);
            if(escritos>=(uint)ruta.Capacity){
                ruta.EnsureCapacity(checked((int)escritos+1));
                escritos=GetFinalPathNameByHandle(archivo.SafeFileHandle,ruta,(uint)ruta.Capacity,0);
            }
            if(escritos==0 || escritos>=(uint)ruta.Capacity)throw new IOException("Couldn’t locate the running game’s folder.");
            return ruta.ToString();
        }
    }
    static bool UsaCarpeta(Process proceso,string ruta) {
        try {
            if(proceso.HasExited)return false;
            string carpeta=Path.GetDirectoryName(proceso.MainModule.FileName);
            return String.Equals(RutaMotor(carpeta),RutaMotor(ruta),StringComparison.OrdinalIgnoreCase);
        } catch(InvalidOperationException) {return true;}
        catch(System.ComponentModel.Win32Exception) {return true;}
        catch(UnauthorizedAccessException) {return true;}
        catch(IOException) {return true;}
    }
    internal static string Cambiar(string ruta,bool instalar) {
        using(var mutex=new Mutex(false,@"Local\FF8RemasteredHorizonteInstaller")) {
            bool lockHeld;
            try { lockHeld=mutex.WaitOne(0); } catch(AbandonedMutexException) { lockHeld=true; }
            Exigir(lockHeld,"Another installer is running. Wait for it to finish.");
            try { return Aplicar(ruta,instalar); } finally { mutex.ReleaseMutex(); }
        }
    }
    static string Aplicar(string ruta,bool instalar) {
        Exigir(Directory.Exists(ruta),"Choose the FINAL FANTASY VIII Remastered folder.");
        Exigir(!EsGog(ruta),"GOG edition detected. This renderer currently supports the Steam engine only.");
        Exigir(Firma(Path.Combine(ruta,"FFVIII_EFIGS.dll"))==Motor,"Choose a compatible Steam installation of FFVIII Remastered for Windows (EFIGS).");
        foreach(var nombre in new[]{"FFVIII","FFVIII_LAUNCHER"}) {
            var procesos=Process.GetProcessesByName(nombre);
            bool enUso=false;
            foreach(var proceso in procesos)using(proceso){enUso|=UsaCarpeta(proceso,ruta);}
            Exigir(!enUso,"Close FFVIII and its launcher to update this installation.");
        }
        var audio=Path.Combine(ruta,"libvgmstream.dll");
        var copia=Path.Combine(ruta,"libvgmstream_orig.dll");
        var marca=Path.Combine(ruta,"FF8_geometry.installed");
        var activo=Firma(audio);
        bool actualizar=EsAnterior(activo);
        Exigir(activo==Original||activo==Mod||actualizar,"A different version or DLL is installed. Use its own uninstaller first.");
        Exigir(!File.Exists(Path.Combine(ruta,"FF8_geometry.pending")),"Another installation is pending in this folder.");
        Exigir(!File.Exists(copia)||Firma(copia)==Original,"The backup does not match the original DLL.");
        string version=File.Exists(marca)?File.ReadAllText(marca).Trim():"";
        Exigir(version==""||version==Mod||EsAnterior(version),"This folder contains an installation marker from another mod.");
        if(activo!=Original) Exigir(File.Exists(copia),"The original DLL backup needed to restore the game is missing.");
        if(instalar && activo==Mod) {
            if(version!=Mod) File.WriteAllText(marca,Mod);
            return LimpiarActualizaciones(ruta,"This version is already installed and verified.");
        }
        if(!instalar && activo==Original) {
            if(File.Exists(marca)) File.Delete(marca);
            if(File.Exists(copia)) File.Delete(copia);
            return LimpiarActualizaciones(ruta,"The game already has its original DLL.");
        }
        var etapa=Path.Combine(ruta,"FF8-Horizonte-"+Guid.NewGuid().ToString("N")+".tmp");
        var anterior=etapa+".bak";
        try {
            if(instalar) {
                using(var destino=new FileStream(etapa,FileMode.CreateNew,FileAccess.Write))
                using(var recurso=Assembly.GetExecutingAssembly().GetManifestResourceStream("mod.gz"))
                using(var datos=new GZipStream(recurso,CompressionMode.Decompress)) datos.CopyTo(destino);
                Exigir(Firma(etapa)==Mod,"The installer files failed verification.");
                // File.Replace creates the original backup in the same operation.
                File.Replace(etapa,audio,actualizar?anterior:File.Exists(copia)?null:copia);
                try {
                    Exigir(Firma(audio)==Mod && Firma(copia)==Original,"The installation could not be verified.");
                    File.WriteAllText(marca,Mod);
                } catch {
                    File.Replace(actualizar?anterior:copia,audio,null);
                    throw;
                }
                return LimpiarActualizaciones(ruta,"Mod installed. You can now launch the game from Steam.");
            }
            File.Copy(copia,etapa);
            Exigir(Firma(etapa)==Original,"The original DLL backup could not be verified.");
            File.Replace(etapa,audio,null);
            Exigir(Firma(audio)==Original,"The restored DLL could not be verified.");
            File.Delete(marca);
            File.Delete(copia);
            return LimpiarActualizaciones(ruta,"Mod uninstalled. The original DLL has been restored.");
        } finally { if(File.Exists(etapa)) File.Delete(etapa); if(File.Exists(anterior)&&Firma(audio)==Mod) File.Delete(anterior); }
    }
}

internal sealed class Ventana : Form {
    readonly ComboBox edicion=new ComboBox();
    readonly Label tienda;
    readonly TextBox carpeta=new TextBox();
    readonly MenuLabel estado=new MenuLabel();
    readonly Button instalar=new MenuButton(), quitar=new MenuButton();
    readonly Portada portada=new Portada();
    Label Rotulo(string texto,int x,int y,int w,int h,float pixels,Color color) {
        var label=new MenuLabel{Text=texto,ForeColor=color,Pixels=pixels};
        label.SetBounds(x,y,w,h);Controls.Add(label);return label;
    }
    void Boton(Button button,string text,int x,int y,int width) {
        button.Text=text;button.SetBounds(x,y,width,36);Controls.Add(button);
    }
    protected override void OnPaint(PaintEventArgs e) {
        base.OnPaint(e);
        var g=e.Graphics;
        var previous=g.Save();
        float scale=g.DpiX/96f;
        g.TranslateTransform(AutoScrollPosition.X,AutoScrollPosition.Y);
        g.ScaleTransform(scale,scale);
        ClassicUI.Box(g,new Rectangle(272,33,532,191));
        ClassicUI.Box(g,new Rectangle(282,13,126,32));
        ClassicUI.Box(g,new Rectangle(272,229,532,104));
        ClassicUI.Box(g,new Rectangle(16,342,788,64));
        g.Restore(previous);
    }
    public Ventana(string ruta) : this(ruta,Juego.DetectarGog()) {}
    internal Ventana(string ruta,string gog) {
        Text="FF8 Reborn · Installer";
        AutoScaleDimensions=new SizeF(96,96);AutoScaleMode=AutoScaleMode.Dpi;
        Font=new Font("Segoe UI",10);ClientSize=new Size(820,420);
        DoubleBuffered=true;BackColor=ClassicUI.Black;ForeColor=ClassicUI.Ink;
        FormBorderStyle=FormBorderStyle.FixedDialog; MaximizeBox=false; StartPosition=FormStartPosition.CenterScreen;
        AutoScroll=true;AutoScrollMinSize=new Size(820,420);
        portada.SetBounds(16,33,246,300);Controls.Add(portada);
        Rotulo("Setup",298,15,95,28,28,ClassicUI.Ink);
        tienda=Rotulo("Steam",292,51,260,25,22,ClassicUI.Ice);
        Rotulo("Game folder",292,78,207,26,25,ClassicUI.Ink);
        carpeta.BackColor=Color.FromArgb(19,20,59);carpeta.ForeColor=ClassicUI.Ink;
        carpeta.BorderStyle=BorderStyle.FixedSingle;
        carpeta.SetBounds(292,113,378,27);carpeta.Text=String.IsNullOrEmpty(ruta)?gog:ruta;carpeta.TabIndex=0;
        if(!String.IsNullOrEmpty(ruta)&&!String.IsNullOrEmpty(gog)&&!Juego.EsGog(ruta)){
            edicion.SetBounds(606,57,179,27);edicion.DropDownStyle=ComboBoxStyle.DropDownList;
            edicion.TabIndex=1;edicion.AccessibleName="Game edition";edicion.Items.AddRange(new object[]{"Steam","GOG"});
            edicion.SelectedIndex=0;Controls.Add(edicion);
            edicion.SelectedIndexChanged+=delegate{
                if(edicion.SelectedIndex<0)return;
                carpeta.Text=edicion.SelectedIndex==0?ruta:gog;RevisarCarpeta();
            };
        }
        carpeta.AccessibleName="Game folder";Controls.Add(carpeta);
        var buscar=new MenuButton();Boton(buscar,"Browse…",678,106,109);buscar.TabIndex=2;
        buscar.Click+=delegate {
            using(var dialogo=new FolderBrowserDialog {Description="Select the folder containing FFVIII_EFIGS.dll",ShowNewFolderButton=false}) {
                if(Directory.Exists(carpeta.Text)) dialogo.SelectedPath=carpeta.Text;
                if(dialogo.ShowDialog(this)==DialogResult.OK){carpeta.Text=dialogo.SelectedPath;RevisarCarpeta();}
            }
        };
        carpeta.Validated+=delegate{RevisarCarpeta();};
        carpeta.TextChanged+=delegate{if(Directory.Exists(carpeta.Text.Trim()))RevisarCarpeta();};
        Rotulo("In-game settings",292,243,485,29,27,ClassicUI.Ice);
        var instructions=(MenuLabel)Rotulo("Press F8 on the keyboard or Select + Start on the controller.",292,274,492,43,21,ClassicUI.Ink);
        instructions.Wrap=true;
        Boton(instalar,"Install",292,161,242);instalar.TabIndex=3;
        instalar.Click+=delegate{
            if(!actual){Cambiar(true);return;}
            try {Process.Start(new ProcessStartInfo("steam://rungameid/1026680"){UseShellExecute=true});}
            catch(Exception e){estado.Text=e.Message;}
        };
        Boton(quitar,"Uninstall",632,161,155);quitar.TabIndex=4;quitar.Click+=delegate{Cambiar(false);};
        estado.SetBounds(33,349,756,51);estado.Pixels=21;estado.Wrap=true;estado.ForeColor=ClassicUI.Ink;estado.AccessibleName="Installation status";Controls.Add(estado);
        RevisarCarpeta();ActiveControl=instalar;
    }
    protected override void OnLoad(EventArgs e) {
        base.OnLoad(e);
        Size limite=Screen.FromControl(this).WorkingArea.Size;
        if(Width>limite.Width || Height>limite.Height)
            Size=new Size(Math.Min(Width,limite.Width),Math.Min(Height,limite.Height));
    }
    string ultimaCarpeta;
    bool actual;
    void ActualizarAcciones(bool instalado) {
        actual=instalado;
        instalar.Text=actual?"Play on Steam":"Install";
        quitar.Visible=actual||File.Exists(Path.Combine(carpeta.Text.Trim(),"FF8_geometry.installed"));
    }
    void RevisarCarpeta() {
        string ruta=carpeta.Text.Trim();
        if(String.Equals(ruta,ultimaCarpeta,StringComparison.OrdinalIgnoreCase))return;
        try {
            bool gog=Juego.EsGog(ruta);
            if(edicion.Items.Count>0 && edicion.SelectedIndex>=0 && (edicion.SelectedIndex==1)!=gog)edicion.SelectedIndex=-1;
            tienda.Text=gog?"GOG edition":"Steam edition";
            instalar.Enabled=!gog;
            if(gog){
                actual=false;instalar.Text="GOG support pending";quitar.Visible=false;
                estado.Text="GOG installation found. Its engine still needs compatibility testing.";ultimaCarpeta=ruta;return;
            }
            ActualizarAcciones(Juego.VersionActual(ruta));
            ultimaCarpeta=ruta;
            estado.Text=actual?"FF8 Reborn is installed.":"Close the game and its launcher before installing.";
        } catch(Exception e){ActualizarAcciones(false);estado.Text=e.Message;}
    }
    void Cambiar(bool quiereInstalar) {
        UseWaitCursor=true;
        try {
            estado.Text=Juego.Cambiar(carpeta.Text.Trim(),quiereInstalar);
            ActualizarAcciones(quiereInstalar);

        }
        catch(UnauthorizedAccessException) { estado.Text="Windows denied access to this folder. Close the installer, then choose Run as administrator."; }
        catch(Exception error) { estado.Text=error.Message; }
        finally { UseWaitCursor=false; }
    }
}
internal static class Programa {
    [STAThread] static void Main() {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new Ventana(Juego.Detectar()));
    }
}
