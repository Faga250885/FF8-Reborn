using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Reflection;
using System.Windows.Forms;

internal sealed class Portada : Panel {
    readonly VistaAtlasRetro vista;
    static readonly Color mar=Color.FromArgb(8,37,46), oro=Color.FromArgb(221,190,129);
    internal Portada() {
        DoubleBuffered=true; BackColor=mar;
        using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream("atlas.jpg"))
        using(var img=Image.FromStream(s)) vista=new VistaAtlasRetro(img);
        AccessibleName="Screen filter preview on the world map. FF8 Reborn by Faga12.";
    }
    internal void Actualizar(int modo,int[] valores,bool rasterPS1,bool hdBlend) { vista.Actualizar(modo,valores,rasterPS1,hdBlend);Invalidate(); }
    protected override void Dispose(bool disposing) { if(disposing) vista.Dispose();base.Dispose(disposing); }
    static void Texto(Graphics g,string t,string family,float px,FontStyle style, Color color,float x,float y) {
        using(var f=new Font(family,px,style,GraphicsUnit.Pixel))
        using(var b=new SolidBrush(color)) g.DrawString(t,f,b,x,y,StringFormat.GenericTypographic);
    }
    protected override void OnPaint(PaintEventArgs e) {
        base.OnPaint(e);
        var g=e.Graphics;g.ScaleTransform(Width/230f,Height/552f);
        g.TextRenderingHint=TextRenderingHint.AntiAliasGridFit;g.SmoothingMode=SmoothingMode.AntiAlias;
        using(var pen=new Pen(oro,1)) {
            g.DrawLine(pen,20,26,56,26);g.DrawPolygon(pen,new[]{new Point(60,23),new Point(63,26),new Point(60,29),new Point(57,26)});
            g.DrawLine(pen,20,471,210,471);
        }
        Texto(g,"FF8","Georgia",44,FontStyle.Regular,oro,18,40);
        Texto(g,"Reborn","Georgia",55,FontStyle.Regular,Color.Ivory,17,81);
        Texto(g,"FINAL FANTASY VIII REMASTERED","Segoe UI",10,FontStyle.Bold,Color.FromArgb(190,211,207),20,145);
        Texto(g,"FILTER PREVIEW","Segoe UI",10,FontStyle.Bold,oro,20,194);
        g.InterpolationMode=InterpolationMode.NearestNeighbor;g.PixelOffsetMode=PixelOffsetMode.Half;
        g.DrawImage(vista.Imagen,new Rectangle(12,215,206,217),new Rectangle(0,0,206,217),GraphicsUnit.Pixel);
        Texto(g,"UNOFFICIAL MOD","Segoe UI",10,FontStyle.Bold,oro,20,489);
        Texto(g,"Created by Faga12","Segoe UI",15,FontStyle.Regular,Color.Ivory,20,510);
    }
}
