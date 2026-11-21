using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal sealed partial class Ventana {
    static readonly string[] clavesRetro={"Lines","Pixelation","Scanlines","Mask","Glow","Dither","EdgeSoftness"};
    static readonly int[] retroSuave={240,85,35,20,10,20,0}, retroPixel={240,100,0,0,0,100,0},
                          retroMarcado={240,100,52,24,4,100,8}, retroFondos={240,100,0,0,0,20,65};
    static readonly int[][] perfiles={retroSuave,retroPixel,retroSuave,retroMarcado,retroFondos,
        new[]{240,100,23,14,8,100,42},new[]{360,80,0,0,0,30,32},new[]{480,32,0,0,0,6,70},new[]{320,100,20,20,8,70,40}};
    static readonly int[] modos={0,1,2,2,1,2,1,1,2};
    const int personalizada=9,recomendada=8;
    readonly NumericUpDown[] controlesRetro=new NumericUpDown[clavesRetro.Length];
    readonly Label[] nombresRetro=new Label[clavesRetro.Length], unidadesRetro=new Label[clavesRetro.Length];
    readonly ToolTip ayudaRetro=new ToolTip();
    readonly int[][] guardados=new int[10][];
    readonly int[] modosGuardados=new int[10];
    readonly bool[] rasterGuardado=new bool[10],pendiente=new bool[10],quitarOverride=new bool[10];
    int origen,basePersonalizada=recomendada,modoActual;
    bool configurandoRetro,rasterPS1,borrador,existePersonalizado;
    static bool UsaRasterPS1(int perfil){return perfil==1||perfil==3||perfil==5;}
    int ModoRetro {get{return modoActual;}}
    void CrearControlesRetro() {
        Rotulo("Screen filter",583,178,167,24,11,tinta,true);
        var opciones=new Button{Text="Profile options ▾",TabIndex=5,AccessibleName="Profile options",FlatStyle=FlatStyle.Flat,ForeColor=verde};
        opciones.SetBounds(744,176,168,28);Controls.Add(opciones);opciones.BringToFront();
        var acciones=new ContextMenuStrip();
        var recuperar=acciones.Items.Add("Restore filter defaults",null,delegate{
            int id=origen==personalizada?basePersonalizada:origen;
            modoActual=modos[id];rasterPS1=UsaRasterPS1(id);MostrarRetro(origen,perfiles[id]);GuardarEn(origen);
            quitarOverride[origen]=origen!=personalizada;
        });
        var guardarCopia=acciones.Items.Add("Create a custom copy",null,delegate{
            GuardarEn(personalizada);
            estado.Text="Custom copy ready. Choose Save settings to keep it.";
        });
        acciones.Opening+=delegate{
            int id=origen==personalizada?basePersonalizada:origen;
            recuperar.Visible=modoActual!=modos[id]||rasterPS1!=UsaRasterPS1(id)||!System.Linq.Enumerable.SequenceEqual(ValoresRetro(),perfiles[id]);
            guardarCopia.Text=origen==personalizada?"Save my changes":existePersonalizado?"Save to my custom profile":"Create a custom copy";
            guardarCopia.ToolTipText=origen!=personalizada&&existePersonalizado?"This will replace your saved custom profile.":"Save these settings to your custom profile.";
        };
        opciones.Click+=delegate{acciones.Show(opciones,0,opciones.Height);};
        ayudaRetro.SetToolTip(opciones,"Create your own copy of this filter. In game, press F8 or Select + Start to open settings.");
        opciones.Disposed+=delegate{acciones.Dispose();};
        Lista(perfilRetro,new[]{"Off","PS1 Digital","Soft CRT","PS1 · RGB Monitor","PS1 Backgrounds","PS1 · CRT TV","PS1 Sharp","HD Blend","FF8 Reborn (recommended)","Custom"},
              584,210,328,6,"Screen filter");
        string[] nombres={"Vertical resolution","Pixelation","Scanlines","RGB mask","Glow","PS1 dithering","Edge smoothing"};
        string[] ayudas={
            "Nominal height. In the classic presets, 240 lines correspond to 224 visible lines on the expanded world map and 216 in battle.",
            "Controls how strongly the image snaps to the virtual pixel grid.",
            "Darkens the gaps between scanlines.",
            "Controls the visibility of the red, green and blue phosphor pattern.",
            "Spreads light from bright areas into neighboring pixels.",
            "Blends colors using ordered dithering to simulate the PlayStation palette.",
            "Softens edges to blend character models with prerendered backgrounds."
        };
        for(int i=0;i<controlesRetro.Length;i++) {
            int x=584+(i%2)*170, y=249+(i/2)*59;
            bool ultimaFila=i==6;
            nombresRetro[i]=Rotulo(nombres[i],x,ultimaFila?y+5:y,162,19,9,tinta,true);
            var n=new NumericUpDown();
            n.Minimum=i==0?120:0;n.Maximum=i==0?720:100;
            n.TabIndex=7+i;n.AccessibleName=nombres[i]+(i==0?" in lines":" as a percentage");
            n.SetBounds(ultimaFila?x+170:x,ultimaFila?y:y+22,91,27);Controls.Add(n);controlesRetro[i]=n;
            unidadesRetro[i]=Rotulo(i==0?"lines":"%",n.Left+98,n.Top+2,55,23,9,suave,false);
            ayudaRetro.SetToolTip(n,ayudas[i]);ayudaRetro.SetToolTip(nombresRetro[i],ayudas[i]);
            n.ValueChanged+=delegate {
                if(configurandoRetro) return;
                borrador=true;configurandoRetro=true;
                try { perfilRetro.SelectedIndex=personalizada; } finally { configurandoRetro=false; }
                ActualizarVistaRetro();
            };
        }
        perfilRetro.SelectedIndexChanged+=delegate {
            if(configurandoRetro||perfilRetro.SelectedIndex<0)return;
            int destino=perfilRetro.SelectedIndex;
            if(borrador){
                configurandoRetro=true;
                try { perfilRetro.SelectedIndex=personalizada; } finally { configurandoRetro=false; }
                estado.Text="Open Profile options to save your copy or restore this filter’s defaults.";
                return;
            }
            Seleccionar(destino);
        };
        Disposed+=delegate { ayudaRetro.Dispose(); };
    }
    int[] ValoresRetro() {
        return Array.ConvertAll(controlesRetro,delegate(NumericUpDown n){return Decimal.ToInt32(n.Value);});
    }
    void Seleccionar(int id){
        origen=id;modoActual=modosGuardados[id];rasterPS1=rasterGuardado[id];borrador=false;
        MostrarRetro(id,guardados[id]);
    }
    void GuardarEn(int id){
        if(id==personalizada)existePersonalizado=true;
        if(id==personalizada&&origen!=personalizada)basePersonalizada=origen;
        guardados[id]=ValoresRetro();modosGuardados[id]=modoActual;rasterGuardado[id]=rasterPS1;pendiente[id]=true;quitarOverride[id]=false;
        Seleccionar(id);
    }
    void ActualizarVistaRetro() {
        int modo=ModoRetro;
        for(int i=0;i<controlesRetro.Length;i++) {
            bool activo=modo==2 || (modo==1 && (i<2 || i>=5));
            controlesRetro[i].Enabled=activo;
            nombresRetro[i].Enabled=activo;
            unidadesRetro[i].Enabled=activo;
        }
        portada.Actualizar(modo,ValoresRetro(),rasterPS1,(origen==personalizada?basePersonalizada:origen)==7);
    }
    void MostrarRetro(int perfil,int[] valores) {
        configurandoRetro=true;
        try {
            for(int i=0;i<valores.Length;i++) {
                var n=controlesRetro[i];n.Value=Math.Min(n.Maximum,Math.Max(n.Minimum,valores[i]));
            }
            perfilRetro.SelectedIndex=perfil;
        } finally { configurandoRetro=false; }
        ActualizarVistaRetro();
    }
    void CargarRetro(string ruta) {
        existePersonalizado=GetPrivateProfileInt("RetroPerfil9","Mode",-1,ruta)>=0;
        int baseId=GetPrivateProfileInt("Retro","CustomBase",recomendada,ruta);
        basePersonalizada=baseId>=0&&baseId<personalizada?baseId:recomendada;
        for(int i=0;i<=personalizada;i++){
            string seccion="RetroPerfil"+i;int id=i==personalizada?basePersonalizada:i;
            modosGuardados[i]=GetPrivateProfileInt(seccion,"Mode",modos[id],ruta);
            rasterGuardado[i]=GetPrivateProfileInt(seccion,"PS1Raster",UsaRasterPS1(id)?1:0,ruta)!=0;
            guardados[i]=LeerValores(ruta,seccion,perfiles[id]);pendiente[i]=quitarOverride[i]=false;
        }
        int seleccionado=GetPrivateProfileInt("Retro","Selected",-1,ruta);
        if(seleccionado<0||seleccionado>personalizada){
            int modo=GetPrivateProfileInt("Retro","Mode",int.MinValue,ruta);
            bool raster=GetPrivateProfileInt("Retro","PS1Raster",0,ruta)!=0;
            int[] valores=LeerValores(ruta,"Retro",modo==int.MinValue?perfiles[recomendada]:retroSuave);
            if(modo==int.MinValue)modo=2;
            seleccionado=0;
            if(modo!=0){
                seleccionado=personalizada;
                for(int i=1;i<personalizada;i++){
                    if(modos[i]!=modo||UsaRasterPS1(i)!=raster)continue;
                    bool igual=true;
                    for(int k=0;k<7;k++)if((modo==2||k<2||k>=5)&&valores[k]!=perfiles[i][k])igual=false;
                    if(igual){seleccionado=i;break;}
                }
            }
            if(seleccionado==personalizada){
                existePersonalizado=true;
                guardados[personalizada]=valores;modosGuardados[personalizada]=modo;
                rasterGuardado[personalizada]=raster;pendiente[personalizada]=true;
            }
        }
        Seleccionar(seleccionado);
    }
    int[] LeerValores(string ruta,string seccion,int[] inicial){
        var valores=new int[clavesRetro.Length];
        for(int i=0;i<valores.Length;i++)valores[i]=Math.Min((int)controlesRetro[i].Maximum,Math.Max((int)controlesRetro[i].Minimum,GetPrivateProfileInt(seccion,clavesRetro[i],inicial[i],ruta)));
        return valores;
    }
    void EscribirFiltro(string ruta,string seccion,int modo,bool raster,int[] valores){
        EscribirClave(ruta,seccion,"Mode",modo);EscribirClave(ruta,seccion,"PS1Raster",raster?1:0);
        for(int i=0;i<valores.Length;i++)EscribirClave(ruta,seccion,clavesRetro[i],valores[i]);
    }
    void EscribirRetro(string ruta) {
        if(borrador)GuardarEn(personalizada);
        for(int i=0;i<=personalizada;i++)if(pendiente[i]){
            if(quitarOverride[i]){
                if(!WritePrivateProfileString("RetroPerfil"+i,null,null,ruta))throw new System.ComponentModel.Win32Exception();
            }else EscribirFiltro(ruta,"RetroPerfil"+i,modosGuardados[i],rasterGuardado[i],guardados[i]);
        }
        EscribirFiltro(ruta,"Retro",ModoRetro,rasterPS1,ValoresRetro());
        EscribirClave(ruta,"Retro","Selected",origen);EscribirClave(ruta,"Retro","CustomBase",basePersonalizada);
    }
}

// A close crop illustrates pixels at display scale even in a small installer window.
internal sealed class VistaAtlasRetro : IDisposable {
    const int ancho=206,alto=217;
    static readonly int[] bayer={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    readonly Bitmap original=new Bitmap(ancho,alto,PixelFormat.Format32bppArgb),
                    filtrado=new Bitmap(ancho,alto,PixelFormat.Format32bppArgb);
    readonly int[] fuente=new int[ancho*alto],salida=new int[ancho*alto];
    internal Bitmap Imagen {get;private set;}
    internal VistaAtlasRetro(Image atlas) {
        using(var g=Graphics.FromImage(original)) {
            g.InterpolationMode=InterpolationMode.HighQualityBicubic;
            g.DrawImage(atlas,new Rectangle(0,0,ancho,alto),new Rectangle(146,66,360,377),GraphicsUnit.Pixel);
        }
        BitmapData data=original.LockBits(new Rectangle(0,0,ancho,alto),ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
        try {Marshal.Copy(data.Scan0,fuente,0,fuente.Length);}
        finally {original.UnlockBits(data);}
        Imagen=original;
    }
    public void Dispose() { original.Dispose();filtrado.Dispose(); }
    static double Canal(int color,int shift) {return (color>>shift)&255;}
    static double Mezclar(double a,double b,double fraccion) {return a+(b-a)*fraccion;}
    static double Trama(double canal,double umbral,double intensidad) {
        double cuantizado=Math.Min(31,Math.Max(0,Math.Floor(canal*31/255+umbral)))*255/31;
        return Mezclar(canal,cuantizado,intensidad);
    }
    static double PesoDelBorde(double contraste,double intensidad) {
        double t=Math.Max(0,Math.Min(1,(contraste/255-0.06)/0.22));
        return 0.82*intensidad*t*t*(3-2*t);
    }
    static readonly int[] matrizPS1={-4,0,-3,1,2,-2,3,-1,-3,1,-4,0,3,-1,2,-2};
    internal void Actualizar(int modo,int[] v,bool rasterPS1,bool hdBlend) {
        if(modo==0) { Imagen=original;return; }
        double celda=900.0/v[0],pixelado=v[1]*0.01,scan=modo==2?v[2]*0.01:0,
            mascara=modo==2?v[3]*0.01:0,brillo=modo==2?v[4]*0.01:0,trama=v[5]*0.01,suavizado=v[6]*0.01;
        int radio=Math.Max(1,(int)Math.Round((hdBlend?2.1:1.25)*celda));
        int cercano=Math.Max(1,(int)Math.Round(.72*celda));
        double[] desplazamiento=new double[3];
        double[] centrales=new double[3],vecinos=new double[3];
        for(int y=0;y<alto;y++) {
            double fila=(y+0.5)/celda;
            double muestraY=Math.Max(0,Math.Min(alto-1,Mezclar(y,(Math.Floor(fila)+0.5)*celda-0.5,pixelado)));
            int y0=(int)muestraY,y1=Math.Min(alto-1,y0+1);
            double linea=1-0.65*scan*(0.5+0.5*Math.Cos(fila*2*Math.PI));
            for(int x=0;x<ancho;x++) {
                double columna=(x+0.5)/celda;
                double muestraX=Math.Max(0,Math.Min(ancho-1,Mezclar(x,(Math.Floor(columna)+0.5)*celda-0.5,pixelado)));
                int x0=(int)muestraX,x1=Math.Min(ancho-1,x0+1);
                int a=fuente[y0*ancho+x0], b=fuente[y0*ancho+x1], c=fuente[y1*ancho+x0], d=fuente[y1*ancho+x1];
                double umbral=(bayer[(((int)fila&3)*4)+((int)columna&3)]+0.5)/16;
                int izq=fuente[y0*ancho+Math.Max(0,x0-radio)],der=fuente[y0*ancho+Math.Min(ancho-1,x0+radio)];
                int arriba=fuente[Math.Max(0,y0-radio)*ancho+x0],abajo=fuente[Math.Min(alto-1,y0+radio)*ancho+x0];
                double contraste=0;
                for(int canal=0;canal<3;canal++) {
                    int shift=(2-canal)*8;
                    double superior=Mezclar(Canal(a,shift),Canal(b,shift),muestraX-x0);
                    double inferior=Mezclar(Canal(c,shift),Canal(d,shift),muestraX-x0);
                    double centro=Mezclar(superior,inferior,muestraY-y0);
                    double l=Canal(izq,shift),r=Canal(der,shift),u=Canal(arriba,shift),dn=Canal(abajo,shift);
                    if(suavizado>0) {
                        contraste=Math.Max(contraste,Math.Max(Math.Abs(centro-l),Math.Abs(centro-r)));
                        contraste=Math.Max(contraste,Math.Max(Math.Abs(centro-u),Math.Abs(centro-dn)));
                    }
                    centrales[canal]=centro;
                    vecinos[canal]=(l+r+u+dn)*0.25;
                }
                double peso=PesoDelBorde(contraste,suavizado);
                double mezclaHD=0,luz=0;
                if(hdBlend&&suavizado>0){
                    int l=fuente[y0*ancho+Math.Max(0,x0-cercano)],r=fuente[y0*ancho+Math.Min(ancho-1,x0+cercano)];
                    int u=fuente[Math.Max(0,y0-cercano)*ancho+x0],dwn=fuente[Math.Min(alto-1,y0+cercano)*ancho+x0];
                    double pico=0;
                    for(int cidx=0;cidx<3;cidx++){
                        int shift=(2-cidx)*8;
                        double local=(Canal(l,shift)+Canal(r,shift)+Canal(u,shift)+Canal(dwn,shift))*.25;
                        pico=Math.Max(pico,Math.Abs(local-centrales[cidx])/255);
                        desplazamiento[cidx]=Mezclar(local,vecinos[cidx],.46)-centrales[cidx];
                    }
                    double t=Math.Max(0,Math.Min(1,(pico-.008)/.067));
                    mezclaHD=suavizado*t*t*(3-2*t);
                    luz=.2126*desplazamiento[0]+.7152*desplazamiento[1]+.0722*desplazamiento[2];
                }
                int color=unchecked((int)0xff000000);
                for(int canal=0;canal<3;canal++) {
                    double baseColor=Mezclar(centrales[canal],vecinos[canal],peso);
                    if(hdBlend)baseColor=Math.Max(0,Math.Min(255,centrales[canal]+mezclaHD*(
                        luz/(1+Math.Abs(luz)/51)+Math.Max(-56.1,Math.Min(56.1,desplazamiento[canal]-luz)))));
                    double valor;
                    if(rasterPS1){
                        int nivel=(int)Math.Max(0,Math.Min(255,Math.Floor(baseColor+0.5)+matrizPS1[(((int)fila&3)*4)+((int)columna&3)]))/8;
                        valor=Mezclar(baseColor,nivel*255.0/31,trama);
                    }else valor=Trama(baseColor,umbral,trama);
                    valor*=linea*(x%3==canal?1:1-0.35*mascara);
                    valor+=brillo*0.35*vecinos[canal];
                    color|=Math.Min(255,(int)(valor+0.5))<<((2-canal)*8);
                }
                salida[y*ancho+x]=color;
            }
        }
        Rectangle area=new Rectangle(0,0,ancho,alto);
        BitmapData data=filtrado.LockBits(area,ImageLockMode.WriteOnly,PixelFormat.Format32bppArgb);
        try {Marshal.Copy(salida,0,data.Scan0,salida.Length);}
        finally {filtrado.UnlockBits(data);}
        Imagen=filtrado;
    }
}
