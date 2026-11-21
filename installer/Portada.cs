using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.Reflection;
using System.Windows.Forms;

internal sealed class Portada : Panel {
    readonly Bitmap atlas;
    static readonly Color mar=Color.FromArgb(8,37,46), oro=Color.FromArgb(221,190,129);
    internal Portada() {
        DoubleBuffered=true; BackColor=mar;
        using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream("atlas.jpg"))
        using(var img=Image.FromStream(s)) atlas=new Bitmap(img);
        AccessibleName="FF8 Reborn world map. Created by Faga12.";
    }
    protected override void Dispose(bool disposing) { if(disposing) atlas.Dispose();base.Dispose(disposing); }
    static void Texto(Graphics g,string t,string family,float px,FontStyle style, Color color,float x,float y) {
        using(var f=new Font(family,px,style,GraphicsUnit.Pixel))
        using(var b=new SolidBrush(color)) g.DrawString(t,f,b,x,y,StringFormat.GenericTypographic);
    }
    protected override void OnPaint(PaintEventArgs e) {
        base.OnPaint(e);
        var g=e.Graphics;g.ScaleTransform(Width/230f,Height/407f);
        g.TextRenderingHint=TextRenderingHint.AntiAliasGridFit;g.SmoothingMode=SmoothingMode.AntiAlias;
        using(var pen=new Pen(oro,1)) {
            g.DrawLine(pen,20,26,56,26);g.DrawPolygon(pen,new[]{new Point(60,23),new Point(63,26),new Point(60,29),new Point(57,26)});
            g.DrawLine(pen,20,331,210,331);
        }
        Texto(g,"FF8","Georgia",44,FontStyle.Regular,oro,18,40);
        Texto(g,"Reborn","Georgia",55,FontStyle.Regular,Color.Ivory,17,81);
        Texto(g,"FINAL FANTASY VIII REMASTERED","Segoe UI",10,FontStyle.Bold,Color.FromArgb(190,211,207),20,145);
        g.InterpolationMode=InterpolationMode.NearestNeighbor;g.PixelOffsetMode=PixelOffsetMode.Half;
        g.DrawImage(atlas,new Rectangle(12,171,206,143));
        Texto(g,"UNOFFICIAL MOD","Segoe UI",10,FontStyle.Bold,oro,20,346);
        Texto(g,"Created by Faga12","Segoe UI",15,FontStyle.Regular,Color.Ivory,20,365);
    }
}
