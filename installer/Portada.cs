using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;

internal static class ClassicUI {
    internal static readonly Color Ink=Color.FromArgb(248,248,239), Ice=Color.FromArgb(162,245,255),
        Muted=Color.FromArgb(197,201,233), Black=Color.FromArgb(10,10,27);
    static readonly PrivateFontCollection fonts=new PrivateFontCollection();
    static readonly IntPtr fontData;
    // Keep the memory alive for the lifetime of the GDI+ private font collection.
    static ClassicUI() {
        using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("FF8Menu.ttf"))
        using(var data=new MemoryStream()){
            stream.CopyTo(data);
            byte[] bytes=data.ToArray();fontData=Marshal.AllocHGlobal(bytes.Length);
            Marshal.Copy(bytes,0,fontData,bytes.Length);fonts.AddMemoryFont(fontData,bytes.Length);
        }
    }
    internal static Bitmap Picture(string name) {
        using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
        using(var image=Image.FromStream(stream))return new Bitmap(image);
    }
    internal static void Text(Graphics g,string text,RectangleF bounds,float pixels,Color color,bool wrap=false,bool center=false){
        using(var font=new Font(fonts.Families[0],pixels,FontStyle.Regular,GraphicsUnit.Pixel))
        using(var shadow=new SolidBrush(Color.FromArgb(24,21,54)))
        using(var brush=new SolidBrush(color))
        using(var format=(StringFormat)StringFormat.GenericTypographic.Clone()){
            format.Trimming=StringTrimming.EllipsisCharacter;
            format.FormatFlags=wrap?0:StringFormatFlags.NoWrap;
            format.Alignment=center?StringAlignment.Center:StringAlignment.Near;
            format.LineAlignment=StringAlignment.Center;
            g.TextRenderingHint=TextRenderingHint.SingleBitPerPixelGridFit;
            RectangleF offset=bounds;offset.Offset(Math.Max(1,pixels/22),Math.Max(1,pixels/22));
            g.DrawString(text,font,shadow,offset,format);
            g.DrawString(text,font,brush,bounds,format);
        }
    }
    static readonly int[] Blue={0x8383ce,0x7b7bc8,0x7373c2,0x6868b8,0x5959ae,0x5050a6,0x4646a0,0x3d3d99,0x313192,0x232389,0x161683,0x00007b,0x000073,0x00006b,0x000063,0x000057};
    internal static void Box(Graphics g,Rectangle r){
        if(r.Width<10||r.Height<10)return;
        int x=r.X,y=r.Y,w=r.Width,h=r.Height;
        using(var shadow=new SolidBrush(Color.FromArgb(47,47,67)))
        using(var edge=new SolidBrush(Color.FromArgb(133,133,151)))
        using(var light=new SolidBrush(Color.FromArgb(224,224,232))){
            g.FillRectangle(shadow,x+3,y,w-6,h);g.FillRectangle(shadow,x,y+3,w,h-6);
            g.FillRectangle(edge,x+2,y+2,w-4,h-4);
            g.FillRectangle(light,x+4,y+1,w-8,2);g.FillRectangle(light,x+1,y+4,2,h-8);
        }
        for(int i=0;i<Blue.Length;i++){
            int y0=4+(h-8)*i/Blue.Length,y1=4+(h-8)*(i+1)/Blue.Length;
            using(var band=new SolidBrush(Color.FromArgb(unchecked((int)0xff000000)|Blue[i])))g.FillRectangle(band,x+4,y+y0,w-8,y1-y0);
        }
    }
    internal static void ImageNearest(Graphics g,Image image,Rectangle dest){
        var old=g.InterpolationMode;var offset=g.PixelOffsetMode;
        g.InterpolationMode=InterpolationMode.NearestNeighbor;g.PixelOffsetMode=PixelOffsetMode.Half;
        g.DrawImage(image,dest,0,0,image.Width,image.Height,GraphicsUnit.Pixel);
        g.InterpolationMode=old;g.PixelOffsetMode=offset;
    }
}

internal sealed class MenuLabel : Label {
    internal float Pixels=20;
    internal bool Wrap=false;
    internal MenuLabel(){
        BackColor=Color.Transparent;AutoSize=false;UseMnemonic=false;
        SetStyle(ControlStyles.UserPaint|ControlStyles.SupportsTransparentBackColor|ControlStyles.OptimizedDoubleBuffer,true);
    }
    protected override void OnPaint(PaintEventArgs e){
        float scale=e.Graphics.DpiX/96f;
        ClassicUI.Text(e.Graphics,Text,ClientRectangle,Pixels*scale,ForeColor,Wrap);
    }
}

internal sealed class MenuButton : Button {
    bool hot;
    internal MenuButton(){
        FlatStyle=FlatStyle.Flat;FlatAppearance.BorderSize=0;
        SetStyle(ControlStyles.UserPaint|ControlStyles.AllPaintingInWmPaint|ControlStyles.OptimizedDoubleBuffer,true);
    }
    protected override void OnEnter(EventArgs e){base.OnEnter(e);Invalidate();}
    protected override void OnLeave(EventArgs e){base.OnLeave(e);Invalidate();}
    protected override void OnMouseEnter(EventArgs e){base.OnMouseEnter(e);hot=true;Invalidate();}
    protected override void OnMouseLeave(EventArgs e){base.OnMouseLeave(e);hot=false;Invalidate();}
    protected override void OnPaint(PaintEventArgs e){
        var g=e.Graphics;ClassicUI.Box(g,ClientRectangle);
        float s=g.DpiX/96f;
        var textBounds=new RectangleF(4,1,Width-8,Height-2);
        if(Enabled&&(Focused||hot))
            using(var pen=new Pen(ClassicUI.Ice,Math.Max(1,(int)Math.Round(s))))
                g.DrawRectangle(pen,4,4,Width-9,Height-9);
        ClassicUI.Text(g,Text,textBounds,23*s,Enabled?(hot||Focused?ClassicUI.Ice:ClassicUI.Ink):ClassicUI.Muted,false,true);
    }
}

internal sealed class Portada : Panel {
    readonly Bitmap atlas=ClassicUI.Picture("atlas.jpg");
    internal Portada(){
        DoubleBuffered=true;BackColor=ClassicUI.Black;
        AccessibleName="FF8 Reborn world map. Created by Faga12.";
    }
    protected override void Dispose(bool disposing){if(disposing)atlas.Dispose();base.Dispose(disposing);}
    protected override void OnPaint(PaintEventArgs e){
        var g=e.Graphics;g.ScaleTransform(Width/246f,Height/300f);
        ClassicUI.Box(g,new Rectangle(0,0,246,300));
        ClassicUI.Text(g,"FF8 Reborn",new RectangleF(17,11,222,49),43,ClassicUI.Ink);
        ClassicUI.Text(g,"Final Fantasy VIII Remastered",new RectangleF(18,58,217,25),18,ClassicUI.Ice);
        ClassicUI.ImageNearest(g,atlas,new Rectangle(13,94,220,154));
        ClassicUI.Text(g,"Created by Faga12",new RectangleF(17,257,214,31),22,ClassicUI.Ink);
    }
}
