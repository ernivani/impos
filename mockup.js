// ===== ImposOS Desktop Mockup — Liquid Glass Edition =====

// ===== LIQUID GLASS LAYER INJECTION =====
// Injects the SVG lens filter + the three glass layers into existing elements
(function initLiquidGlass() {
  // SVG displacement filter
  const svgWrap = document.createElement('div');
  svgWrap.className = 'lg-svg-filters';
  svgWrap.innerHTML = `
    <svg xmlns="http://www.w3.org/2000/svg">
      <filter id="lensFilter" x="-5%" y="-5%" width="110%" height="110%" filterUnits="objectBoundingBox">
        <!-- Procedural noise for organic glass surface -->
        <feTurbulence type="fractalNoise" baseFrequency="0.015 0.012"
          numOctaves="2" seed="3" result="noise"/>
        <!-- Concentrate noise into lens-like spots via gamma curve -->
        <feComponentTransfer in="noise" result="shaped">
          <feFuncR type="gamma" amplitude="1" exponent="6" offset="0.5"/>
          <feFuncG type="gamma" amplitude="1" exponent="6" offset="0.5"/>
        </feComponentTransfer>
        <!-- Smooth the displacement map -->
        <feGaussianBlur in="shaped" stdDeviation="5" result="softMap"/>
        <!-- Specular highlights from the glass surface curvature -->
        <feSpecularLighting in="softMap" surfaceScale="3" specularConstant="0.75"
          specularExponent="80" lighting-color="white" result="specLight">
          <fePointLight x="-150" y="-100" z="300"/>
        </feSpecularLighting>
        <!-- Displace source through the glass -->
        <feDisplacementMap in="SourceGraphic" in2="softMap" scale="30"
          xChannelSelector="R" yChannelSelector="G" result="refracted"/>
        <!-- Blend subtle specular caustics over refracted image -->
        <feComposite in="specLight" in2="refracted" operator="arithmetic"
          k1="0" k2="0.12" k3="1" k4="0"/>
      </filter>
    </svg>`;
  document.body.appendChild(svgWrap);

  // Inject glass layers into an element (prepends blur/overlay/specular divs)
  function injectGlassLayers(el) {
    el.classList.add('lg-panel');
    const blur = document.createElement('div');
    blur.className = 'lg-blur';
    const overlay = document.createElement('div');
    overlay.className = 'lg-overlay';
    const specular = document.createElement('div');
    specular.className = 'lg-specular';
    el.prepend(specular, overlay, blur);
  }

  // Apply to menubar
  injectGlassLayers(document.getElementById('menubar'));

  // Apply to drawer
  injectGlassLayers(document.getElementById('drawer'));

  // Apply to radial launcher
  injectGlassLayers(document.getElementById('radial'));

  // Expose for dynamically created elements (windows, context menus)
  window._injectGlassLayers = injectGlassLayers;
})();

// ===== WALLPAPER ENGINE =====
const wp = document.getElementById('wp');
const wctx = wp.getContext('2d');
let W = window.innerWidth, H = window.innerHeight;
wp.width = W * 2; wp.height = H * 2;
wctx.scale(2, 2);

function lerp(a,b,t){return a+(b-a)*t}
function lerpColor(c1,c2,t){
  const p=s=>{const n=parseInt(s.slice(1),16);return[(n>>16)&255,(n>>8)&255,n&255]};
  const [r1,g1,b1]=p(c1),[r2,g2,b2]=p(c2);
  return `rgb(${lerp(r1,r2,t)|0},${lerp(g1,g2,t)|0},${lerp(b1,b2,t)|0})`;
}
function ease(t){return t<0.5?2*t*t:1-Math.pow(-2*t+2,2)/2}

const starField = Array.from({length:120},()=>({x:Math.random(),y:Math.random()*0.6,s:Math.random()*1.5+0.5,b:Math.random()}));

// ===== DRAW FUNCTIONS =====
function drawMountains(ctx,w,h,themeA,themeB,et){
  const A=themeA, B=themeB;
  const maxStops=Math.max(A.sky.length,B.sky.length);
  const grad=ctx.createLinearGradient(0,0,0,h);
  for(let i=0;i<maxStops;i++){
    const cA=A.sky[Math.min(i,A.sky.length-1)];
    const cB=B.sky[Math.min(i,B.sky.length-1)];
    grad.addColorStop(i/(maxStops-1),lerpColor(cA,cB,et));
  }
  ctx.fillStyle=grad;ctx.fillRect(0,0,w,h);
  const gx=lerp(A.glow.x,B.glow.x,et)*w;
  const gy=lerp(A.glow.y,B.glow.y,et)*h;
  const gr=lerp(A.glow.r,B.glow.r,et)*(w/1400);
  const rg=ctx.createRadialGradient(gx,gy,0,gx,gy,gr);
  rg.addColorStop(0,`rgba(200,120,80,${lerp(0.15,0.3,et)})`);
  rg.addColorStop(1,'rgba(0,0,0,0)');
  ctx.fillStyle=rg;ctx.fillRect(0,0,w,h);
  const sa=lerp(A.stars,B.stars,et);
  if(sa>0.01){
    const now=Date.now();
    starField.forEach(s=>{
      const flicker=0.6+0.4*Math.sin(now*0.002+s.b*10);
      ctx.globalAlpha=sa*flicker*0.7;
      ctx.fillStyle='#fff';
      ctx.beginPath();ctx.arc(s.x*w,s.y*h,s.s*(w/1400+0.3),0,Math.PI*2);ctx.fill();
    });
    ctx.globalAlpha=1;
  }
  for(let layer=0;layer<3;layer++){
    const baseY=h*0.55+layer*(h*0.033);
    const cA=A.mtn[Math.min(layer,A.mtn.length-1)];
    const cB=B.mtn[Math.min(layer,B.mtn.length-1)];
    ctx.fillStyle=lerpColor(cA,cB,et);
    ctx.beginPath();ctx.moveTo(0,h);
    const step=Math.max(1,Math.ceil(w/500)*3);
    for(let x=0;x<=w;x+=step){
      const nx=x/w;
      const freq1=3+layer, freq2=7+layer*2;
      const hv=Math.sin(nx*freq1*Math.PI+layer*2)*40+Math.sin(nx*freq2*Math.PI+layer)*20+Math.sin(nx*15*Math.PI)*10;
      ctx.lineTo(x,baseY-hv*(1-layer*0.2)*(h/800));
    }
    ctx.lineTo(w,h);ctx.closePath();ctx.fill();
  }
  const aa=lerp(A.aurora,B.aurora,et);
  if(aa>0.01){
    const now=Date.now();
    ctx.globalAlpha=aa*0.15;
    for(let i=0;i<3;i++){
      const ag=ctx.createLinearGradient(0,0,w,0);
      ag.addColorStop(0,'rgba(0,255,100,0)');ag.addColorStop(0.3,'rgba(0,255,150,0.4)');
      ag.addColorStop(0.5,'rgba(0,200,255,0.3)');ag.addColorStop(0.7,'rgba(100,0,255,0.2)');
      ag.addColorStop(1,'rgba(0,255,100,0)');
      ctx.fillStyle=ag;
      const ay=h*0.15+i*(h*0.035)+Math.sin(now*0.0005+i)*(h*0.025);
      ctx.beginPath();ctx.moveTo(0,ay+h*0.05);
      for(let x=0;x<=w;x+=Math.max(2,w/300)){
        const wave=Math.sin(x/w*5*Math.PI+now*0.001+i)*(h*0.03)+Math.sin(x/w*10*Math.PI-now*0.0005)*(h*0.018);
        ctx.lineTo(x,ay+wave);
      }
      ctx.lineTo(w,ay+h*0.05);ctx.closePath();ctx.fill();
    }
    ctx.globalAlpha=1;
  }
}

function drawGradient(ctx,w,h,themeA,themeB,et){
  const now=Date.now();
  const colors=themeA.colors.map((c,i)=>lerpColor(c,themeB.colors[Math.min(i,themeB.colors.length-1)],et));
  const shift=now*0.00003;
  const grad=ctx.createLinearGradient(
    w*0.5+Math.sin(shift)*w*0.4,0,
    w*0.5+Math.cos(shift+1)*w*0.4,h
  );
  colors.forEach((c,i)=>grad.addColorStop(i/(colors.length-1),c));
  ctx.fillStyle=grad;ctx.fillRect(0,0,w,h);
  for(let i=0;i<3;i++){
    const ox=w*(0.3+0.4*Math.sin(shift*1.5+i*2.1));
    const oy=h*(0.3+0.4*Math.cos(shift*1.2+i*1.7));
    const og=ctx.createRadialGradient(ox,oy,0,ox,oy,w*0.25);
    og.addColorStop(0,'rgba(255,255,255,0.04)');og.addColorStop(1,'rgba(0,0,0,0)');
    ctx.fillStyle=og;ctx.fillRect(0,0,w,h);
  }
}

function drawGeometric(ctx,w,h,themeA,themeB,et){
  const bg=lerpColor(themeA.bg,themeB.bg,et);
  ctx.fillStyle=bg;ctx.fillRect(0,0,w,h);
  const cols=Math.ceil(w/60)+1;
  const rows=Math.ceil(h/52)+1;
  const sz=Math.max(w/cols,60);
  const now=Date.now();
  const palette=themeA.palette.map((c,i)=>lerpColor(c,themeB.palette[Math.min(i,themeB.palette.length-1)],et));
  for(let row=0;row<rows;row++){
    for(let col=0;col<cols;col++){
      const x=col*sz+(row%2?sz/2:0);
      const y=row*sz*0.866;
      const ci=(col*7+row*13)%palette.length;
      const pulse=0.5+0.5*Math.sin(now*0.001+col*0.3+row*0.5);
      ctx.globalAlpha=0.15+pulse*0.15;
      ctx.fillStyle=palette[ci];
      ctx.beginPath();ctx.moveTo(x,y-sz*0.4);
      ctx.lineTo(x+sz*0.5,y+sz*0.2);ctx.lineTo(x-sz*0.5,y+sz*0.2);
      ctx.closePath();ctx.fill();
      ctx.beginPath();ctx.moveTo(x,y+sz*0.4);
      ctx.lineTo(x+sz*0.5,y-sz*0.0);ctx.lineTo(x-sz*0.5,y-sz*0.0);
      ctx.closePath();ctx.fill();
    }
  }
  ctx.globalAlpha=1;
}

function drawStars(ctx,w,h,themeA,themeB,et){
  const bg=lerpColor(themeA.bg,themeB.bg,et);
  ctx.fillStyle=bg;ctx.fillRect(0,0,w,h);
  const now=Date.now();
  const nebColors=themeA.nebula.map((c,i)=>lerpColor(c,themeB.nebula[Math.min(i,themeB.nebula.length-1)],et));
  nebColors.forEach((c,i)=>{
    const nx=w*(0.3+0.4*Math.sin(now*0.00004+i*1.8));
    const ny=h*(0.25+0.3*Math.cos(now*0.00003+i*2.2));
    const nr=w*0.3;
    const ng=ctx.createRadialGradient(nx,ny,0,nx,ny,nr);
    ng.addColorStop(0,c);ng.addColorStop(1,'rgba(0,0,0,0)');
    ctx.fillStyle=ng;ctx.fillRect(0,0,w,h);
  });
  const starCount=200;
  const seed=themeA.seed||42;
  for(let i=0;i<starCount;i++){
    const sx=((i*1237+seed*31)%10000)/10000;
    const sy=((i*4729+seed*17)%10000)/10000;
    const ss=((i*3571+seed*7)%10000)/10000*1.8+0.3;
    const sb=((i*8123+seed*3)%10000)/10000;
    const flicker=0.5+0.5*Math.sin(now*0.003+sb*20);
    ctx.globalAlpha=flicker*0.8;
    ctx.fillStyle='#fff';
    ctx.beginPath();ctx.arc(sx*w,sy*h,ss*(w/1400+0.3),0,Math.PI*2);ctx.fill();
  }
  ctx.globalAlpha=1;
}

function drawWaves(ctx,w,h,themeA,themeB,et){
  const skyA=themeA.sky, skyB=themeB.sky;
  const maxStops=Math.max(skyA.length,skyB.length);
  const grad=ctx.createLinearGradient(0,0,0,h);
  for(let i=0;i<maxStops;i++){
    grad.addColorStop(i/(maxStops-1),lerpColor(skyA[Math.min(i,skyA.length-1)],skyB[Math.min(i,skyB.length-1)],et));
  }
  ctx.fillStyle=grad;ctx.fillRect(0,0,w,h);
  const now=Date.now();
  const layers=themeA.waves.length;
  for(let l=0;l<layers;l++){
    const cA=themeA.waves[l], cB=themeB.waves[Math.min(l,themeB.waves.length-1)];
    ctx.fillStyle=lerpColor(cA,cB,et);
    const baseY=h*0.4+l*(h*0.12);
    const speed=(l+1)*0.0004;
    const amp=h*0.04*(1+l*0.3);
    ctx.beginPath();ctx.moveTo(0,h);
    const step=Math.max(2,w/400);
    for(let x=0;x<=w;x+=step){
      const nx=x/w;
      const y=baseY+Math.sin(nx*4*Math.PI+now*speed)*amp+Math.sin(nx*7*Math.PI-now*speed*0.7)*amp*0.4
        +Math.sin(nx*2*Math.PI+now*speed*0.3+l)*amp*0.3;
      ctx.lineTo(x,y);
    }
    ctx.lineTo(w,h);ctx.closePath();ctx.fill();
  }
}

// ===== WALLPAPER STYLES REGISTRY =====
const WALLPAPER_STYLES = {
  mountains: {
    label: 'Mountains',
    themes: [
      {name:'Night',sky:['#0a0e1a','#0d1328','#121a35','#1a1f3a'],mtn:['#0c1020','#0e1225','#10152a'],glow:{x:0.5,y:0.7,r:400},stars:0.9,aurora:0.4,dot:'#1a1f3a'},
      {name:'Dawn',sky:['#1a1025','#2d1530','#6b3040','#c87050','#e8a870'],mtn:['#1a1020','#251828','#352030'],glow:{x:0.5,y:0.85,r:500},stars:0.15,aurora:0,dot:'#c87050'},
      {name:'Day',sky:['#2a6ac0','#3a80d0','#60a0e0','#90c0f0','#b0d8ff'],mtn:['#3a5570','#4a6580','#5a7590'],glow:{x:0.6,y:0.3,r:350},stars:0,aurora:0,dot:'#60a0e0'},
      {name:'Dusk',sky:['#1a1028','#3a1830','#7a3535','#c06530','#d09040'],mtn:['#1a1020','#251520','#352025'],glow:{x:0.3,y:0.75,r:450},stars:0.05,aurora:0,dot:'#c06530'},
    ],
    draw: drawMountains,
  },
  gradient: {
    label: 'Gradient',
    themes: [
      {name:'Sunset',colors:['#1a0530','#6b1040','#d04020','#f08030','#ffd060'],dot:'#d04020'},
      {name:'Ocean',colors:['#020818','#0a2848','#1060a0','#20a0d0','#60d0e0'],dot:'#1060a0'},
      {name:'Aurora',colors:['#0a1020','#103040','#10806a','#40c080','#80f0a0'],dot:'#40c080'},
      {name:'Midnight',colors:['#08060e','#150828','#281048','#401868','#602888'],dot:'#401868'},
    ],
    draw: drawGradient,
  },
  geometric: {
    label: 'Geometric',
    themes: [
      {name:'Dark',bg:'#0a0c12',palette:['#1a2030','#202840','#283050','#303860','#384070'],dot:'#283050'},
      {name:'Colorful',bg:'#10101a',palette:['#4030a0','#a03060','#d06020','#30a070','#2060c0'],dot:'#a03060'},
      {name:'Neon',bg:'#05050a',palette:['#ff0080','#00ff80','#0080ff','#ff8000','#8000ff'],dot:'#ff0080'},
    ],
    draw: drawGeometric,
  },
  stars: {
    label: 'Stars',
    themes: [
      {name:'Deep Space',bg:'#020208',nebula:['rgba(20,10,60,0.3)','rgba(60,10,40,0.2)'],seed:42,dot:'#1a1040'},
      {name:'Nebula',bg:'#050210',nebula:['rgba(80,20,120,0.35)','rgba(20,60,120,0.3)','rgba(120,30,60,0.2)'],seed:77,dot:'#6020a0'},
      {name:'Starfield',bg:'#000005',nebula:['rgba(10,20,40,0.15)'],seed:123,dot:'#101830'},
    ],
    draw: drawStars,
  },
  waves: {
    label: 'Waves',
    themes: [
      {name:'Ocean',sky:['#081828','#103050','#1860a0','#2090d0'],waves:['#0a3060','#0c4080','#1050a0','#1868b8','#2080d0'],dot:'#1860a0'},
      {name:'Sunset Sea',sky:['#1a0820','#501030','#a03030','#d06030','#e8a050'],waves:['#301020','#501828','#702030','#903038','#a84040'],dot:'#d06030'},
      {name:'Arctic',sky:['#101820','#182838','#284050','#406070','#608090'],waves:['#182830','#203840','#284850','#305860','#406878'],dot:'#406070'},
    ],
    draw: drawWaves,
  },
};

let curStyleId = 'mountains';
let curThemeIdx = 0;
let curTheme = 0, tgtTheme = 0, transT = 1;

function getCurStyle(){ return WALLPAPER_STYLES[curStyleId]; }

function setWallpaperStyle(styleId, themeIdx){
  const style = WALLPAPER_STYLES[styleId];
  if(!style) return;
  curStyleId = styleId;
  curThemeIdx = themeIdx != null ? themeIdx : 0;
  curTheme = curThemeIdx;
  tgtTheme = curThemeIdx;
  transT = 1;
  saveWallpaperPref();
}

window.setTheme = function(idx){
  const style = getCurStyle();
  if(!style || idx < 0 || idx >= style.themes.length) return;
  if(idx===tgtTheme&&transT>=1)return;
  curTheme=tgtTheme;tgtTheme=idx;transT=0;
  curThemeIdx = idx;
  saveWallpaperPref();
};

function drawCurrentWallpaper(){
  const style = getCurStyle();
  if(!style) return;
  const et = ease(Math.min(transT,1));
  const A = style.themes[curTheme];
  const B = style.themes[tgtTheme];
  style.draw(wctx, W, H, A, B, et);
}

function animLoop(){
  if(transT<1)transT=Math.min(transT+0.008,1);
  drawCurrentWallpaper();
  requestAnimationFrame(animLoop);
}

// ===== WALLPAPER PERSISTENCE =====
const WP_STORAGE_KEY = 'imposos_wallpaper';

function saveWallpaperPref(){
  localStorage.setItem(WP_STORAGE_KEY, JSON.stringify({style:curStyleId, theme:curThemeIdx}));
}

function loadWallpaperPref(){
  try{
    const data = JSON.parse(localStorage.getItem(WP_STORAGE_KEY));
    if(data && WALLPAPER_STYLES[data.style]){
      curStyleId = data.style;
      const style = getCurStyle();
      curThemeIdx = Math.min(data.theme||0, style.themes.length-1);
      curTheme = curThemeIdx;
      tgtTheme = curThemeIdx;
      transT = 1;
    }
  }catch(e){}
}
loadWallpaperPref();
animLoop();

// Clock
// ===== SYSTEM TRAY =====
(function initTray() {
  const tray = document.getElementById('tray');
  if (!tray) return;
  tray.innerHTML = `
    <svg viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M8 2C4.5 2 1.5 4.2 0 7.5c1.5 3.3 4.5 5.5 8 5.5s6.5-2.2 8-5.5C14.5 4.2 11.5 2 8 2z" fill="none" stroke="white" stroke-width="1.3"/>
      <circle cx="8" cy="7.5" r="2.5" fill="white" opacity="0.8"/>
    </svg>
    <svg viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M1 12h2l1.5-4L7 11l2-6 2 8 1.5-5L14 12h1" fill="none" stroke="white" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/>
    </svg>
    <svg viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M8 1.5C4.8 1.5 2 3 0 5.5l2.2 2C3.7 6 5.7 4.8 8 4.8s4.3 1.2 5.8 2.7l2.2-2C14 3 11.2 1.5 8 1.5z" fill="white" opacity="0.4"/>
      <path d="M8 5.5c-2 0-3.8 0.8-5 2.2L5 9.5c.8-.9 2-1.5 3-1.5s2.2.6 3 1.5l2-1.8C11.8 6.3 10 5.5 8 5.5z" fill="white" opacity="0.6"/>
      <circle cx="8" cy="12" r="1.5" fill="white" opacity="0.9"/>
    </svg>
    <svg viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M3 13V5M6 13V3M9 13V7M12 13V4" stroke="white" stroke-width="1.8" stroke-linecap="round"/>
    </svg>
    <svg viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
      <rect x="1.5" y="4" width="11" height="8" rx="1.5" fill="none" stroke="white" stroke-width="1.3"/>
      <rect x="12.5" y="6" width="2" height="4" rx="0.5" fill="white" opacity="0.5"/>
      <rect x="2.8" y="5.3" width="5" height="5.4" rx="0.5" fill="white" opacity="0.35"/>
    </svg>
  `;
})();

// ===== CLOCK =====
setInterval(()=>{
  const d=new Date();
  document.getElementById('clock').textContent=
    d.toLocaleDateString('en-US',{weekday:'short',month:'short',day:'numeric'})+
    '  '+d.toLocaleTimeString('en-US',{hour:'2-digit',minute:'2-digit'});
},1000);

// ===== RADIAL LAUNCHER =====
const overlay = document.getElementById('radial-overlay');
const radEl = document.getElementById('radial');
const rc = document.getElementById('rc');
const rctx = rc.getContext('2d');
const R = 180;
const IR = 110;
const CR = 42;
const ICON = 46;
const ROUND = 12;
const PI2 = Math.PI * 2;
const MAX_RING = 8;

// ===== APP CATALOG =====
function svgIcon(inner) {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 26 26" width="26" height="26">${inner}</svg>`;
}
const ICONS = {
  terminal: svgIcon(`<path d="M5 7l6 6-6 6" fill="none" stroke="white" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/><line x1="13" y1="19" x2="21" y2="19" stroke="white" stroke-width="2.2" stroke-linecap="round"/>`),
  files: svgIcon(`<path d="M3 7a2 2 0 012-2h5l2 2h9a2 2 0 012 2v10a2 2 0 01-2 2H5a2 2 0 01-2-2V7z" fill="none" stroke="white" stroke-width="1.8"/>`),
  browser: svgIcon(`<circle cx="13" cy="13" r="9" fill="none" stroke="white" stroke-width="1.8"/><ellipse cx="13" cy="13" rx="4.5" ry="9" fill="none" stroke="white" stroke-width="1.5"/><line x1="4" y1="13" x2="22" y2="13" stroke="white" stroke-width="1.5"/>`),
  music: svgIcon(`<ellipse cx="10" cy="18" rx="3.5" ry="2.8" fill="white"/><line x1="13.5" y1="18" x2="13.5" y2="5" stroke="white" stroke-width="2"/><path d="M13.5 5c3 0 5.5 2 5.5 4.5" fill="none" stroke="white" stroke-width="2" stroke-linecap="round"/>`),
  settings: svgIcon(`<path d="M9.5 6.9L11.2 3.2L14.8 3.2L16.5 6.9L20.6 6.5L22.4 9.7L20.0 13.0L22.4 16.3L20.6 19.5L16.5 19.1L14.8 22.8L11.2 22.8L9.5 19.1L5.4 19.5L3.6 16.3L6.0 13.0L3.6 9.7L5.4 6.5Z" fill="none" stroke="white" stroke-width="1.5" stroke-linejoin="round"/><circle cx="13" cy="13" r="3" fill="none" stroke="white" stroke-width="1.6"/>`),
  monitor: svgIcon(`<rect x="3" y="5" width="20" height="13" rx="2" fill="none" stroke="white" stroke-width="1.8"/><line x1="13" y1="18" x2="13" y2="22" stroke="white" stroke-width="1.8"/><line x1="8" y1="22" x2="18" y2="22" stroke="white" stroke-width="1.8" stroke-linecap="round"/>`),
  email: svgIcon(`<rect x="3" y="6" width="20" height="14" rx="2" fill="none" stroke="white" stroke-width="1.8"/><path d="M3 8l10 6 10-6" fill="none" stroke="white" stroke-width="1.8"/>`),
  chat: svgIcon(`<path d="M4 5h18a1 1 0 011 1v11a1 1 0 01-1 1h-4l-4 4v-4H4a1 1 0 01-1-1V6a1 1 0 011-1z" fill="none" stroke="white" stroke-width="1.8"/>`),
  video: svgIcon(`<rect x="3" y="6" width="14" height="14" rx="2" fill="none" stroke="white" stroke-width="1.8"/><path d="M17 10l6-3v12l-6-3z" fill="none" stroke="white" stroke-width="1.8"/>`),
  code: svgIcon(`<path d="M8 7l-5 6 5 6" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M18 7l5 6-5 6" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><line x1="15" y1="4" x2="11" y2="22" stroke="white" stroke-width="1.5"/>`),
  image: svgIcon(`<rect x="3" y="5" width="20" height="16" rx="2" fill="none" stroke="white" stroke-width="1.8"/><circle cx="9" cy="11" r="2" fill="white"/><path d="M3 18l5-5 4 4 3-3 8 6" fill="none" stroke="white" stroke-width="1.5"/>`),
  pdf: svgIcon(`<path d="M6 3h8l6 6v12a2 2 0 01-2 2H6a2 2 0 01-2-2V5a2 2 0 012-2z" fill="none" stroke="white" stroke-width="1.8"/><path d="M14 3v6h6" fill="none" stroke="white" stroke-width="1.8"/>`),
  gamepad: svgIcon(`<rect x="2" y="8" width="22" height="12" rx="4" fill="none" stroke="white" stroke-width="1.8"/><circle cx="17" cy="12" r="1.5" fill="white"/><circle cx="20" cy="14" r="1.5" fill="white"/><line x1="7" y1="11" x2="7" y2="17" stroke="white" stroke-width="1.8"/><line x1="4" y1="14" x2="10" y2="14" stroke="white" stroke-width="1.8"/>`),
  disk: svgIcon(`<ellipse cx="13" cy="13" rx="10" ry="10" fill="none" stroke="white" stroke-width="1.8"/><circle cx="13" cy="13" r="3.5" fill="none" stroke="white" stroke-width="1.5"/><circle cx="13" cy="13" r="1" fill="white"/>`),
  users: svgIcon(`<circle cx="10" cy="9" r="3.5" fill="none" stroke="white" stroke-width="1.8"/><path d="M3 21c0-4 3-7 7-7s7 3 7 7" fill="none" stroke="white" stroke-width="1.8"/><circle cx="19" cy="9" r="2.5" fill="none" stroke="white" stroke-width="1.5"/><path d="M19 14c3 0 5 2 5 5" fill="none" stroke="white" stroke-width="1.5"/>`),
  download: svgIcon(`<path d="M13 3v14" stroke="white" stroke-width="2" stroke-linecap="round"/><path d="M7 13l6 6 6-6" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><line x1="4" y1="22" x2="22" y2="22" stroke="white" stroke-width="2" stroke-linecap="round"/>`),
  table: svgIcon(`<rect x="3" y="5" width="20" height="16" rx="2" fill="none" stroke="white" stroke-width="1.8"/><line x1="3" y1="10" x2="23" y2="10" stroke="white" stroke-width="1.3"/><line x1="3" y1="15" x2="23" y2="15" stroke="white" stroke-width="1.3"/><line x1="10" y1="5" x2="10" y2="21" stroke="white" stroke-width="1.3"/>`),
  pen: svgIcon(`<path d="M17 3l6 6-14 14H3v-6L17 3z" fill="none" stroke="white" stroke-width="1.8" stroke-linejoin="round"/><line x1="14" y1="6" x2="20" y2="12" stroke="white" stroke-width="1.5"/>`),
  box: svgIcon(`<path d="M3 8l10-5 10 5v10l-10 5-10-5z" fill="none" stroke="white" stroke-width="1.8" stroke-linejoin="round"/><line x1="13" y1="13" x2="13" y2="23" stroke="white" stroke-width="1.5"/><line x1="3" y1="8" x2="13" y2="13" stroke="white" stroke-width="1.5"/><line x1="23" y1="8" x2="13" y2="13" stroke="white" stroke-width="1.5"/>`),
  cat_system: svgIcon(`<circle cx="13" cy="13" r="10" fill="none" stroke="white" stroke-width="1.8"/><path d="M13 3v5" stroke="white" stroke-width="1.5"/><path d="M13 18v5" stroke="white" stroke-width="1.5"/><path d="M3 13h5" stroke="white" stroke-width="1.5"/><path d="M18 13h5" stroke="white" stroke-width="1.5"/><circle cx="13" cy="13" r="3" fill="none" stroke="white" stroke-width="1.5"/>`),
  cat_internet: svgIcon(`<circle cx="13" cy="13" r="10" fill="none" stroke="white" stroke-width="1.8"/><ellipse cx="13" cy="13" rx="5" ry="10" fill="none" stroke="white" stroke-width="1.3"/><line x1="3" y1="10" x2="23" y2="10" stroke="white" stroke-width="1.3"/><line x1="3" y1="16" x2="23" y2="16" stroke="white" stroke-width="1.3"/>`),
  cat_media: svgIcon(`<polygon points="9,5 21,13 9,21" fill="none" stroke="white" stroke-width="2" stroke-linejoin="round"/>`),
  cat_graphics: svgIcon(`<circle cx="8" cy="18" r="4" fill="none" stroke="white" stroke-width="1.8"/><circle cx="18" cy="18" r="4" fill="none" stroke="white" stroke-width="1.8"/><circle cx="13" cy="9" r="4" fill="none" stroke="white" stroke-width="1.8"/>`),
  cat_dev: svgIcon(`<path d="M7 7l-5 6 5 6" fill="none" stroke="white" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/><path d="M19 7l5 6-5 6" fill="none" stroke="white" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>`),
  cat_office: svgIcon(`<path d="M6 3h8l6 6v12a2 2 0 01-2 2H6a2 2 0 01-2-2V5a2 2 0 012-2z" fill="none" stroke="white" stroke-width="1.8"/><line x1="8" y1="13" x2="18" y2="13" stroke="white" stroke-width="1.3"/><line x1="8" y1="17" x2="15" y2="17" stroke="white" stroke-width="1.3"/>`),
  cat_games: svgIcon(`<rect x="2" y="8" width="22" height="12" rx="4" fill="none" stroke="white" stroke-width="1.8"/><circle cx="17" cy="12" r="1.5" fill="white"/><circle cx="20" cy="15" r="1.5" fill="white"/><line x1="7" y1="11" x2="7" y2="17" stroke="white" stroke-width="1.8"/><line x1="4" y1="14" x2="10" y2="14" stroke="white" stroke-width="1.8"/>`),
};

const CATEGORIES = [
  {id:'system',   label:'System',      color:'#3478F6', icon:'cat_system'},
  {id:'internet', label:'Internet',    color:'#5856D6', icon:'cat_internet'},
  {id:'media',    label:'Media',       color:'#FF3B30', icon:'cat_media'},
  {id:'graphics', label:'Graphics',    color:'#FF9500', icon:'cat_graphics'},
  {id:'dev',      label:'Development', color:'#34C759', icon:'cat_dev'},
  {id:'office',   label:'Office',      color:'#AF52DE', icon:'cat_office'},
  {id:'games',    label:'Games',       color:'#00C7BE', icon:'cat_games'},
];

const APP_CATALOG = [
  {id:'terminal',    label:'Terminal',        color:'#3478F6', icon:'terminal', category:'system',   pinned:true,  keywords:['bash','shell','cli','console']},
  {id:'files',       label:'Files',           color:'#34C759', icon:'files',    category:'system',   pinned:true,  keywords:['folder','explorer','nautilus']},
  {id:'settings',    label:'Settings',        color:'#FF9500', icon:'settings', category:'system',   pinned:true,  keywords:['config','preferences','control']},
  {id:'monitor',     label:'Monitor',         color:'#00C7BE', icon:'monitor',  category:'system',   pinned:true,  keywords:['htop','task','processes','cpu']},
  {id:'disk_usage',  label:'Disk Usage',      color:'#5856D6', icon:'disk',     category:'system',   pinned:false, keywords:['storage','space','df']},
  {id:'sysinfo',     label:'System Info',     color:'#3478F6', icon:null,       category:'system',   pinned:false, keywords:['about','hardware','neofetch']},
  {id:'packages',    label:'Packages',        color:'#AF52DE', icon:'box',      category:'system',   pinned:false, keywords:['apt','install','package','manager']},
  {id:'users',       label:'Users',           color:'#FF9500', icon:'users',    category:'system',   pinned:false, keywords:['accounts','permissions','groups']},
  {id:'logs',        label:'Logs',            color:'#8E8E93', icon:null,       category:'system',   pinned:false, keywords:['journal','syslog','dmesg']},
  {id:'browser',     label:'Browser',         color:'#5856D6', icon:'browser',  category:'internet', pinned:true,  keywords:['web','firefox','chrome','surf']},
  {id:'email',       label:'Email',           color:'#3478F6', icon:'email',    category:'internet', pinned:false, keywords:['mail','thunderbird','inbox']},
  {id:'chat',        label:'Chat',            color:'#34C759', icon:'chat',     category:'internet', pinned:false, keywords:['messenger','irc','discord','slack']},
  {id:'torrent',     label:'Torrent',         color:'#FF9500', icon:'download', category:'internet', pinned:false, keywords:['bittorrent','transmission','download']},
  {id:'ftp',         label:'FTP Client',      color:'#8E8E93', icon:null,       category:'internet', pinned:false, keywords:['filezilla','sftp','transfer']},
  {id:'music',       label:'Music',           color:'#FF3B30', icon:'music',    category:'media',    pinned:true,  keywords:['audio','player','mpv','spotify']},
  {id:'video',       label:'Video Player',    color:'#FF6600', icon:'video',    category:'media',    pinned:false, keywords:['vlc','movie','film','mpv']},
  {id:'podcast',     label:'Podcasts',        color:'#AF52DE', icon:null,       category:'media',    pinned:false, keywords:['rss','audio','feed']},
  {id:'recorder',    label:'Screen Recorder', color:'#FF3B30', icon:null,       category:'media',    pinned:false, keywords:['obs','capture','record','screencast']},
  {id:'imageview',   label:'Image Viewer',    color:'#34C759', icon:'image',    category:'media',    pinned:false, keywords:['photo','picture','gallery','feh']},
  {id:'radio',       label:'Radio',           color:'#FF9500', icon:null,       category:'media',    pinned:false, keywords:['stream','fm','online']},
  {id:'photoeditor', label:'Photo Editor',    color:'#FF9500', icon:'image',    category:'graphics', pinned:false, keywords:['gimp','photoshop','edit','paint']},
  {id:'vectordraw',  label:'Vector Draw',     color:'#34C759', icon:'pen',      category:'graphics', pinned:false, keywords:['inkscape','svg','illustrator','draw']},
  {id:'screenshot',  label:'Screenshot',      color:'#3478F6', icon:null,       category:'graphics', pinned:false, keywords:['capture','snip','print screen']},
  {id:'colorpicker', label:'Color Picker',    color:'#FF3B30', icon:null,       category:'graphics', pinned:false, keywords:['eyedropper','hex','rgb']},
  {id:'codeeditor',  label:'Code Editor',     color:'#007ACC', icon:'code',     category:'dev',      pinned:false, keywords:['vscode','vim','nano','text','ide']},
  {id:'gitclient',   label:'Git Client',      color:'#F05032', icon:null,       category:'dev',      pinned:false, keywords:['github','gitlab','version','repo']},
  {id:'database',    label:'Database',        color:'#336791', icon:'table',    category:'dev',      pinned:false, keywords:['sql','sqlite','postgres','mysql']},
  {id:'apitester',   label:'API Tester',      color:'#FF6C37', icon:null,       category:'dev',      pinned:false, keywords:['postman','curl','http','rest']},
  {id:'debugger',    label:'Debugger',        color:'#CC342D', icon:null,       category:'dev',      pinned:false, keywords:['gdb','lldb','breakpoint','trace']},
  {id:'writer',      label:'Writer',          color:'#185ABD', icon:null,       category:'office',   pinned:false, keywords:['word','document','libreoffice','text']},
  {id:'spreadsheet', label:'Spreadsheet',     color:'#107C41', icon:'table',    category:'office',   pinned:false, keywords:['excel','calc','csv','libreoffice']},
  {id:'presenter',   label:'Presenter',       color:'#C43E1C', icon:null,       category:'office',   pinned:false, keywords:['powerpoint','slides','impress']},
  {id:'pdfreader',   label:'PDF Reader',      color:'#EC1C24', icon:'pdf',      category:'office',   pinned:false, keywords:['evince','zathura','document']},
  {id:'notes',       label:'Notes',           color:'#FFD60A', icon:null,       category:'office',   pinned:false, keywords:['memo','jot','obsidian','notepad']},
  {id:'solitaire',   label:'Solitaire',       color:'#34C759', icon:null,       category:'games',    pinned:false, keywords:['cards','klondike','patience']},
  {id:'mines',       label:'Mines',           color:'#8E8E93', icon:null,       category:'games',    pinned:false, keywords:['minesweeper','puzzle']},
  {id:'chess',       label:'Chess',           color:'#1C1C1E', icon:null,       category:'games',    pinned:false, keywords:['board','strategy']},
  {id:'tetris',      label:'Tetris',          color:'#FF3B30', icon:null,       category:'games',    pinned:false, keywords:['blocks','puzzle','arcade']},
  {id:'snake',       label:'Snake',           color:'#34C759', icon:null,       category:'games',    pinned:false, keywords:['classic','arcade','retro']},
];

// ===== ICON CACHE =====
const iconCache = new Map();
function getIconImage(app) {
  if (iconCache.has(app.id)) return iconCache.get(app.id);
  const svgStr = app.icon ? ICONS[app.icon] : null;
  if (svgStr) {
    const img = new Image();
    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svgStr);
    iconCache.set(app.id, img);
    return img;
  }
  iconCache.set(app.id, null);
  return null;
}
APP_CATALOG.forEach(a => getIconImage(a));

// ===== STATE =====
let menuOpen = false;
let hoverIdx = -1;
let centerHover = false;
let searchBuf = '';
let radialRAF = null;
let menuOpenTime = 0;
let launchIdx = -1;
let launchTime = 0;
let drawerOpen = false;

let ringItems = [];

// ===== PERSISTENCE =====
const STORAGE_KEY = 'imposos_pinned';
let pinnedOrder = [];

function savePins() {
  pinnedOrder = APP_CATALOG.filter(a => a.pinned).map(a => a.id)
    .sort((a, b) => {
      const ai = pinnedOrder.indexOf(a), bi = pinnedOrder.indexOf(b);
      if (ai >= 0 && bi >= 0) return ai - bi;
      if (ai >= 0) return -1;
      if (bi >= 0) return 1;
      return 0;
    });
  localStorage.setItem(STORAGE_KEY, JSON.stringify(pinnedOrder));
}

function loadPins() {
  try {
    const stored = JSON.parse(localStorage.getItem(STORAGE_KEY));
    if (Array.isArray(stored) && stored.length > 0) {
      APP_CATALOG.forEach(a => a.pinned = false);
      stored.forEach(id => {
        const app = APP_CATALOG.find(a => a.id === id);
        if (app) app.pinned = true;
      });
      pinnedOrder = stored.filter(id => APP_CATALOG.find(a => a.id === id));
      return;
    }
  } catch(e) {}
  pinnedOrder = APP_CATALOG.filter(a => a.pinned).map(a => a.id);
}

loadPins();

// ===== HELPERS =====
function easeOut(t) { return 1 - Math.pow(1 - t, 3); }

function sliceAngle(i) {
  const n = ringItems.length || 1;
  return (i / n) * PI2 - Math.PI / 2;
}

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return `${(n>>16)&255},${(n>>8)&255},${n&255}`;
}

function getSliceAt(mx, my) {
  const n = ringItems.length;
  if (n === 0) return -1;
  const dx = mx - 180, dy = my - 180;
  const dist = Math.sqrt(dx * dx + dy * dy);
  if (dist < CR || dist > R) return -1;
  let angle = Math.atan2(dy, dx) + Math.PI / 2;
  if (angle < 0) angle += PI2;
  angle = (angle + PI2 / (n * 2)) % PI2;
  return Math.floor((angle / PI2) * n) % n;
}

function navigateSlice(dir) {
  const n = ringItems.length;
  if (n === 0) return;
  if (hoverIdx < 0) {
    hoverIdx = dir > 0 ? 0 : n - 1;
  } else {
    hoverIdx = (hoverIdx + dir + n) % n;
  }
}

function roundedRect(ctx, x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h - r);
  ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  ctx.lineTo(x + r, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

// ===== SEARCH =====
function scoreSearch(app, query) {
  const q = query.toLowerCase();
  const l = app.label.toLowerCase();
  if (l.startsWith(q)) return 100;
  if (l.includes(q)) return 60;
  if (app.keywords && app.keywords.some(k => k.includes(q))) return 40;
  const cat = CATEGORIES.find(c => c.id === app.category);
  if (cat && cat.label.toLowerCase().includes(q)) return 20;
  return 0;
}

// ===== RING POPULATION =====
function populateRingPinned() {
  const ordered = [];
  pinnedOrder.forEach(id => {
    const app = APP_CATALOG.find(a => a.id === id && a.pinned);
    if (app) ordered.push(app);
  });
  APP_CATALOG.forEach(a => {
    if (a.pinned && !ordered.includes(a)) ordered.push(a);
  });
  ringItems = ordered.slice(0, MAX_RING);
  hoverIdx = -1;
  menuOpenTime = Date.now();
}

// ===== DRAWING =====
function drawIcon(ix, iy, app, scale, alpha) {
  rctx.globalAlpha = alpha;
  rctx.save();
  rctx.translate(ix, iy);
  rctx.scale(scale, scale);
  rctx.translate(-ix, -iy);

  const s = ICON * 2;
  const r = ROUND * 2;
  roundedRect(rctx, ix - s / 2, iy - s / 2, s, s, r);
  rctx.fillStyle = app.color;
  rctx.fill();

  const img = getIconImage(app);
  if (img && img.complete) {
    const gs = 28 * 2;
    rctx.drawImage(img, ix - gs / 2, iy - gs / 2, gs, gs);
  } else {
    rctx.font = '700 32px Sora, sans-serif';
    rctx.fillStyle = 'rgba(255,255,255,0.9)';
    rctx.textAlign = 'center';
    rctx.textBaseline = 'middle';
    const letters = app.label.length <= 2 ? app.label : app.label.slice(0, 2);
    rctx.fillText(letters, ix, iy + 2);
  }

  rctx.restore();
  rctx.globalAlpha = 1;
}

function drawRadial() {
  rctx.clearRect(0, 0, 720, 720);
  if (!menuOpen) return;
  const cx = 360, cy = 360;
  const now = Date.now();
  const n = ringItems.length;

  // Background + border handled by liquid glass CSS layers (lg-blur/lg-overlay/lg-specular)

  if (n > 1) {
    for (let i = 0; i < n; i++) {
      const a = sliceAngle(i) - PI2 / (n * 2);
      rctx.beginPath();
      rctx.moveTo(cx + Math.cos(a) * CR * 2, cy + Math.sin(a) * CR * 2);
      rctx.lineTo(cx + Math.cos(a) * R * 2, cy + Math.sin(a) * R * 2);
      rctx.strokeStyle = 'rgba(255,255,255,0.1)';
      rctx.lineWidth = 1;
      rctx.stroke();
    }
  }

  const highlightIdx = launchIdx >= 0 ? launchIdx : hoverIdx;
  if (highlightIdx >= 0 && highlightIdx < n) {
    const a1 = sliceAngle(highlightIdx) - PI2 / (n * 2);
    const a2 = sliceAngle(highlightIdx) + PI2 / (n * 2);
    // White glass tint layer
    rctx.beginPath();
    rctx.arc(cx, cy, CR * 2, a1, a2);
    rctx.arc(cx, cy, R * 2, a2, a1, true);
    rctx.closePath();
    rctx.fillStyle = 'rgba(255,255,255,0.12)';
    rctx.fill();
    // Colored highlight layer
    rctx.beginPath();
    rctx.arc(cx, cy, CR * 2, a1, a2);
    rctx.arc(cx, cy, R * 2, a2, a1, true);
    rctx.closePath();
    const rgb = hexToRgb(ringItems[highlightIdx].color);
    if (launchIdx >= 0) {
      const lt = Math.min(1, (now - launchTime) / 220);
      rctx.fillStyle = `rgba(${rgb},${0.55 * (1 - lt)})`;
    } else {
      rctx.fillStyle = `rgba(${rgb},0.32)`;
    }
    rctx.fill();
  }

  const openElapsed = now - menuOpenTime;
  for (let i = 0; i < n; i++) {
    const a = sliceAngle(i);
    const ix = cx + Math.cos(a) * IR * 2;
    const iy = cy + Math.sin(a) * IR * 2;

    const staggerT = Math.max(0, Math.min(1, (openElapsed - i * 35) / 180));
    const staggerScale = 0.3 + 0.7 * easeOut(staggerT);

    let launchScale = 1;
    if (i === launchIdx) {
      const lt = Math.min(1, (now - launchTime) / 220);
      launchScale = 1 + 0.2 * Math.sin(lt * Math.PI);
    }

    drawIcon(ix, iy, ringItems[i], staggerScale * launchScale, staggerT);
  }

  rctx.textAlign = 'center';
  rctx.textBaseline = 'top';
  rctx.font = '500 16px Sora, sans-serif';
  for (let i = 0; i < n; i++) {
    const a = sliceAngle(i);
    const lx = cx + Math.cos(a) * IR * 2;
    const ly = cy + Math.sin(a) * IR * 2 + ICON + 6;
    const isHov = (i === (launchIdx >= 0 ? launchIdx : hoverIdx));
    rctx.fillStyle = isHov ? 'rgba(255,255,255,1)' : 'rgba(255,255,255,0.55)';
    let label = ringItems[i].label;
    if (label.length > 9) label = label.slice(0, 8) + '\u2026';
    rctx.fillText(label, lx, ly);
  }

  rctx.beginPath();
  rctx.arc(cx, cy, CR * 2, 0, PI2);
  rctx.fillStyle = centerHover ? 'rgba(255,255,255,0.18)' : 'rgba(255,255,255,0.08)';
  rctx.fill();
  rctx.strokeStyle = centerHover ? 'rgba(255,255,255,0.3)' : 'rgba(255,255,255,0.15)';
  rctx.lineWidth = centerHover ? 1.5 : 1;
  rctx.stroke();

  rctx.textAlign = 'center';
  rctx.textBaseline = 'middle';

  if (highlightIdx >= 0 && highlightIdx < n) {
    rctx.font = '600 20px Sora, sans-serif';
    rctx.fillStyle = 'rgba(255,255,255,0.95)';
    rctx.fillText(ringItems[highlightIdx].label, cx, cy);
  } else {
    const ch = centerHover;
    const dotR = ch ? 4 : 3.5;
    const dotGap = 13;
    rctx.fillStyle = ch ? 'rgba(255,255,255,0.6)' : 'rgba(255,255,255,0.3)';
    for (let row = 0; row < 2; row++) {
      for (let col = 0; col < 2; col++) {
        rctx.beginPath();
        rctx.arc(cx + (col - 0.5) * dotGap, cy - 8 + (row - 0.5) * dotGap, dotR, 0, PI2);
        rctx.fill();
      }
    }
    rctx.font = (ch ? '500' : '400') + ' 11px Sora, sans-serif';
    rctx.fillStyle = ch ? 'rgba(255,255,255,0.5)' : 'rgba(255,255,255,0.2)';
    rctx.fillText('All apps', cx, cy + 20);
  }

  if (launchIdx >= 0 && (now - launchTime) > 220) {
    launchIdx = -1;
    closeMenu();
  }
}

// ===== LOOP =====
function radialLoop() {
  if (menuOpen) {
    drawRadial();
    radialRAF = requestAnimationFrame(radialLoop);
  } else {
    radialRAF = null;
  }
}

function startRadialLoop() {
  if (!radialRAF) radialRAF = requestAnimationFrame(radialLoop);
}

function stopRadialLoop() {
  if (radialRAF) { cancelAnimationFrame(radialRAF); radialRAF = null; }
}

// ===== MENU OPEN/CLOSE/LAUNCH =====
function openMenu(x, y) {
  const menuR = 180;
  const mx = Math.max(menuR, Math.min(window.innerWidth - menuR, x));
  const my = Math.max(menuR, Math.min(window.innerHeight - menuR, y));
  radEl.style.left = (mx - menuR) + 'px';
  radEl.style.top = (my - menuR) + 'px';

  menuOpen = true;
  hoverIdx = -1;
  launchIdx = -1;

  populateRingPinned();

  overlay.classList.add('open');
  radEl.classList.add('open');
  startRadialLoop();
}

function closeMenu() {
  menuOpen = false;
  hoverIdx = -1;
  launchIdx = -1;

  overlay.classList.remove('open');
  radEl.classList.remove('open');
  radEl.addEventListener('transitionend', () => {
    stopRadialLoop();
    rctx.clearRect(0, 0, 720, 720);
  }, {once: true});
}

// ===== APP DRAWER =====
function openDrawer(initialSearch) {
  drawerOpen = true;
  const drawer = document.getElementById('drawer');
  const input = document.getElementById('drawer-search');

  input.value = initialSearch || '';
  renderDrawerGrid(initialSearch || '');

  drawer.offsetHeight;
  drawer.classList.add('open');
  setTimeout(() => input.focus(), 60);
}

function closeDrawer() {
  drawerOpen = false;
  const drawer = document.getElementById('drawer');
  drawer.classList.remove('open');
  document.getElementById('drawer-search').value = '';
  document.getElementById('drawer-search').blur();
}

function createTile(app) {
  const tile = document.createElement('div');
  tile.className = 'drawer-tile' + (app.pinned ? ' is-pinned' : '');

  const iconWrap = document.createElement('div');
  iconWrap.className = 'drawer-tile-icon';
  iconWrap.style.backgroundColor = app.color;

  const svgStr = app.icon ? ICONS[app.icon] : null;
  if (svgStr) {
    iconWrap.innerHTML = svgStr;
  } else {
    iconWrap.textContent = app.label.slice(0, 2);
    iconWrap.style.color = 'rgba(255,255,255,0.9)';
    iconWrap.style.fontWeight = '700';
    iconWrap.style.fontSize = '15px';
  }

  if (app.pinned) {
    const badge = document.createElement('div');
    badge.className = 'pin-badge';
    badge.innerHTML = '<svg viewBox="0 0 10 10" fill="none"><polyline points="2,5.5 4.5,8 8,3" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>';
    iconWrap.appendChild(badge);
  }

  const lbl = document.createElement('div');
  lbl.className = 'drawer-tile-label';
  lbl.textContent = app.label;

  tile.appendChild(iconWrap);
  tile.appendChild(lbl);

  tile.addEventListener('click', () => {
    launchApp(app.id);
    tile.classList.add('launching');
    setTimeout(() => closeDrawer(), 300);
  });

  tile.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    e.stopPropagation();
    togglePin(app);
  });

  if (app.pinned) {
    tile.draggable = true;
    tile.addEventListener('dragstart', (e) => {
      e.dataTransfer.setData('text/plain', app.id);
      tile.classList.add('dragging');
    });
    tile.addEventListener('dragend', () => tile.classList.remove('dragging'));
    tile.addEventListener('dragover', (e) => {
      e.preventDefault();
      tile.classList.add('drag-over');
    });
    tile.addEventListener('dragleave', () => tile.classList.remove('drag-over'));
    tile.addEventListener('drop', (e) => {
      e.preventDefault();
      tile.classList.remove('drag-over');
      const fromId = e.dataTransfer.getData('text/plain');
      if (fromId && fromId !== app.id) {
        const fi = pinnedOrder.indexOf(fromId);
        const ti = pinnedOrder.indexOf(app.id);
        if (fi >= 0 && ti >= 0) {
          pinnedOrder.splice(fi, 1);
          pinnedOrder.splice(ti, 0, fromId);
          savePins();
          populateRingPinned();
          const query = document.getElementById('drawer-search').value;
          renderDrawerGrid(query);
        }
      }
    });
  }

  return tile;
}

function togglePin(app) {
  const pinnedCount = APP_CATALOG.filter(a => a.pinned).length;
  if (app.pinned) {
    app.pinned = false;
    pinnedOrder = pinnedOrder.filter(id => id !== app.id);
  } else {
    if (pinnedCount >= MAX_RING) {
      const hint = document.querySelector('.drawer-pin-hint');
      if (hint) {
        hint.style.color = 'rgba(255,80,80,0.6)';
        hint.textContent = `Max ${MAX_RING} pinned`;
        setTimeout(() => { hint.style.color = ''; updatePinHint(); }, 1200);
      }
      return;
    }
    app.pinned = true;
    pinnedOrder.push(app.id);
  }
  savePins();
  populateRingPinned();

  const tiles = document.querySelectorAll('.drawer-tile');
  tiles.forEach(t => {
    if (t.querySelector('.drawer-tile-label')?.textContent === app.label) {
      t.classList.toggle('is-pinned', app.pinned);
      const iconWrap = t.querySelector('.drawer-tile-icon');
      const oldBadge = iconWrap.querySelector('.pin-badge');
      if (app.pinned && !oldBadge) {
        const badge = document.createElement('div');
        badge.className = 'pin-badge';
        badge.innerHTML = '<svg viewBox="0 0 10 10" fill="none"><polyline points="2,5.5 4.5,8 8,3" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>';
        iconWrap.appendChild(badge);
      } else if (!app.pinned && oldBadge) {
        oldBadge.remove();
      }
      t.draggable = app.pinned;
    }
  });
  updatePinHint();
}

function updatePinHint() {
  const hint = document.querySelector('.drawer-pin-hint');
  if (!hint) return;
  const count = APP_CATALOG.filter(a => a.pinned).length;
  hint.textContent = `Right-click to pin \u00b7 ${count} / ${MAX_RING}`;
}

function renderDrawerGrid(query) {
  const grid = document.getElementById('drawer-grid');
  grid.innerHTML = '';
  const q = query.trim();

  const pinnedCount = APP_CATALOG.filter(a => a.pinned).length;
  const hint = document.createElement('div');
  hint.className = 'drawer-pin-hint';
  hint.textContent = `Right-click to pin \u00b7 ${pinnedCount} / ${MAX_RING}`;
  grid.appendChild(hint);

  if (q.length > 0) {
    const scored = APP_CATALOG
      .map(app => ({app, score: scoreSearch(app, q)}))
      .filter(s => s.score > 0)
      .sort((a, b) => b.score - a.score);

    if (scored.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'drawer-empty';
      empty.textContent = 'No apps found';
      grid.appendChild(empty);
      return;
    }

    const section = document.createElement('div');
    section.className = 'drawer-cat';
    const label = document.createElement('div');
    label.className = 'drawer-cat-label';
    label.textContent = `${scored.length} result${scored.length !== 1 ? 's' : ''}`;
    section.appendChild(label);

    const tiles = document.createElement('div');
    tiles.className = 'drawer-tiles';
    scored.forEach(({app}, i) => { const t = createTile(app); t.style.animationDelay = (i * 0.02) + 's'; tiles.appendChild(t); });
    section.appendChild(tiles);
    grid.appendChild(section);
  } else {
    let tileIdx = 0;
    CATEGORIES.forEach(cat => {
      const apps = APP_CATALOG.filter(a => a.category === cat.id);
      if (apps.length === 0) return;

      const section = document.createElement('div');
      section.className = 'drawer-cat';
      const label = document.createElement('div');
      label.className = 'drawer-cat-label';
      label.textContent = cat.label;
      section.appendChild(label);

      const tiles = document.createElement('div');
      tiles.className = 'drawer-tiles';
      apps.forEach(app => { const t = createTile(app); t.style.animationDelay = (tileIdx * 0.018) + 's'; tiles.appendChild(t); tileIdx++; });
      section.appendChild(tiles);
      grid.appendChild(section);
    });
  }
}

document.getElementById('drawer-search').addEventListener('input', (e) => {
  renderDrawerGrid(e.target.value);
});

document.getElementById('drawer').addEventListener('click', (e) => {
  const t = e.target;
  if (t === document.getElementById('drawer') || t.classList.contains('lg-blur')
      || t.classList.contains('lg-overlay') || t.classList.contains('lg-specular')) {
    closeDrawer();
  }
});

function launchItem(idx) {
  if (launchIdx >= 0 || idx < 0 || idx >= ringItems.length) return;
  launchIdx = idx;
  launchTime = Date.now();
  launchApp(ringItems[idx].id);
}

// ===== EVENT HANDLERS =====

let ctxMenuEl = null;

function showContextMenu(x, y) {
  closeContextMenu();
  const items = [
    {label: 'Create Folder', action: () => console.log('Create Folder')},
    {label: 'Create File', action: () => console.log('Create File')},
    {sep: true},
    {label: 'Change Wallpaper', action: () => openSettingsWindow('wallpaper')},
    {label: 'Display Settings', action: () => openSettingsWindow('display')},
    {sep: true},
    {label: 'About ImposOS', action: () => openSettingsWindow('about')},
  ];
  const minimized = openWindows.filter(w => w.minimized);
  if (minimized.length > 0) {
    items.push({sep: true});
    minimized.forEach(w => {
      items.push({label: '\u2191 Show ' + w.title, action: () => restoreAppWindow(w)});
    });
  }
  const menu = document.createElement('div');
  menu.className = 'context-menu lg-panel';
  items.forEach(item => {
    if (item.sep) {
      const sep = document.createElement('div');
      sep.className = 'context-menu-sep';
      menu.appendChild(sep);
    } else {
      const el = document.createElement('div');
      el.className = 'context-menu-item';
      el.textContent = item.label;
      el.addEventListener('click', () => { closeContextMenu(); item.action(); });
      menu.appendChild(el);
    }
  });
  // Inject liquid glass layers
  window._injectGlassLayers(menu);
  menu.style.left = Math.min(x, window.innerWidth - 200) + 'px';
  menu.style.top = Math.min(y, window.innerHeight - 220) + 'px';
  document.body.appendChild(menu);
  menu.offsetHeight;
  menu.classList.add('open');
  ctxMenuEl = menu;
}

function closeContextMenu() {
  if (ctxMenuEl) { ctxMenuEl.remove(); ctxMenuEl = null; }
}

document.addEventListener('click', () => closeContextMenu());

document.addEventListener('contextmenu', (e) => {
  e.preventDefault();
  if (drawerOpen || menuOpen) return;
  if (e.clientY < 28) return;
  showContextMenu(e.clientX, e.clientY);
});

document.querySelector('#menubar .logo').addEventListener('click', (e) => {
  e.stopPropagation();
  closeContextMenu();
  if (menuOpen) { closeMenu(); return; }
  if (drawerOpen) return;
  openMenu(window.innerWidth / 2, window.innerHeight / 2);
});

rc.addEventListener('mousemove', (e) => {
  if (!menuOpen || launchIdx >= 0) return;
  const rect = rc.getBoundingClientRect();
  const mx = e.clientX - rect.left, my = e.clientY - rect.top;
  hoverIdx = getSliceAt(mx, my);
  const dx = mx - 180, dy = my - 180;
  centerHover = Math.sqrt(dx * dx + dy * dy) < CR;
  rc.style.cursor = (centerHover || hoverIdx >= 0) ? 'pointer' : 'default';
});

rc.addEventListener('mouseleave', () => {
  if (!menuOpen) return;
  hoverIdx = -1;
  centerHover = false;
});

rc.addEventListener('click', (e) => {
  if (!menuOpen || launchIdx >= 0) return;
  e.stopPropagation();
  const rect = rc.getBoundingClientRect();
  const mx = e.clientX - rect.left;
  const my = e.clientY - rect.top;
  const dx = mx - 180, dy = my - 180;
  const dist = Math.sqrt(dx * dx + dy * dy);

  if (dist > R) {
    closeMenu();
    return;
  }
  if (dist < CR) {
    closeMenu();
    openDrawer('');
    return;
  }

  const slice = getSliceAt(mx, my);
  if (slice >= 0 && slice < ringItems.length) {
    launchItem(slice);
  }
});

overlay.addEventListener('click', () => {
  if (menuOpen && launchIdx < 0) closeMenu();
});

// Keyboard
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    e.preventDefault();
    closeContextMenu();
    if (openWindows.length > 0) {
      const top = openWindows[openWindows.length - 1];
      closeAppWindow(top.el);
      return;
    }
    if (drawerOpen) { closeDrawer(); return; }
    if (menuOpen) { closeMenu(); return; }
    return;
  }

  if (e.key === 'Tab') {
    e.preventDefault();
    closeContextMenu();
    if (drawerOpen) { closeDrawer(); return; }
    if (menuOpen) closeMenu();
    openDrawer('');
    return;
  }

  if (drawerOpen) return;

  if (e.key === ' ' && !e.ctrlKey && !e.metaKey && !e.altKey
      && document.activeElement?.tagName !== 'INPUT'
      && document.activeElement?.tagName !== 'TEXTAREA') {
    e.preventDefault();
    closeContextMenu();
    if (menuOpen) { closeMenu(); return; }
    openMenu(window.innerWidth / 2, window.innerHeight / 2);
    return;
  }
  if (!menuOpen || launchIdx >= 0) return;

  if (e.key === 'ArrowRight' || e.key === 'ArrowDown') {
    e.preventDefault();
    navigateSlice(1);
    return;
  }
  if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') {
    e.preventDefault();
    navigateSlice(-1);
    return;
  }

  if (e.key === 'Enter') {
    if (hoverIdx >= 0 && hoverIdx < ringItems.length) {
      launchItem(hoverIdx);
    }
    return;
  }

  if (e.key.length === 1 && /[a-zA-Z0-9]/.test(e.key)) {
    const char = e.key;
    closeMenu();
    openDrawer(char);
  }
});

if (!localStorage.getItem(STORAGE_KEY)) {
  const logo = document.querySelector('#menubar .logo');
  logo.classList.add('logo-hint');
  logo.addEventListener('animationend', () => logo.classList.remove('logo-hint'), {once: true});
}

// ===== WINDOW MANAGER =====
const openWindows = [];
let dragState = null;
let resizeState = null;

function updateMenubarWindows() {
  const container = document.getElementById('menubar-windows');
  container.innerHTML = '';
  openWindows.forEach(w => {
    const pill = document.createElement('div');
    pill.className = 'menubar-win-pill';
    pill.dataset.appId = w.appId;
    if (w.minimized) pill.classList.add('minimized-pill');
    pill.textContent = w.title;
    pill.addEventListener('click', (e) => {
      e.stopPropagation();
      if (w.minimized) restoreAppWindow(w);
      else bringToFront(w.el);
    });
    container.appendChild(pill);
  });
}

function bringToFront(win) {
  const maxZ = Math.max(150, ...openWindows.map(w => parseInt(w.el.style.zIndex) || 150));
  win.style.zIndex = maxZ + 1;
}

function openAppWindow(appId, title, width, height, contentBuilder) {
  const existing = openWindows.find(w => w.appId === appId);
  if (existing) {
    if (existing.minimized) restoreAppWindow(existing);
    else bringToFront(existing.el);
    return existing.el;
  }

  const win = document.createElement('div');
  win.className = 'app-window lg-panel';
  win.style.width = width + 'px';
  win.style.height = height + 'px';
  win.style.left = Math.round((window.innerWidth - width) / 2) + 'px';
  win.style.top = Math.round((window.innerHeight - height) / 2) + 'px';
  win.style.zIndex = 150 + openWindows.length;

  // Inject liquid glass layers into window
  window._injectGlassLayers(win);

  const titlebar = document.createElement('div');
  titlebar.className = 'app-window-titlebar';

  const trafficLights = document.createElement('div');
  trafficLights.className = 'app-window-trafficlights';

  const closeBtn = document.createElement('button');
  closeBtn.className = 'app-window-btn close';
  closeBtn.addEventListener('click', (e) => { e.stopPropagation(); closeAppWindow(win); });

  const minBtn = document.createElement('button');
  minBtn.className = 'app-window-btn minimize';
  minBtn.addEventListener('click', (e) => { e.stopPropagation(); minimizeAppWindow(win); });

  const maxBtn = document.createElement('button');
  maxBtn.className = 'app-window-btn maximize';
  maxBtn.addEventListener('click', (e) => { e.stopPropagation(); toggleFullscreen(win); });

  trafficLights.appendChild(closeBtn);
  trafficLights.appendChild(minBtn);
  trafficLights.appendChild(maxBtn);

  const titleEl = document.createElement('span');
  titleEl.className = 'app-window-title';
  titleEl.textContent = title;

  titlebar.appendChild(trafficLights);
  titlebar.appendChild(titleEl);

  const body = document.createElement('div');
  body.className = 'app-window-body';

  const edges = ['r-right','r-bottom','r-left','r-top','r-br','r-bl','r-tr','r-tl'];
  edges.forEach(cls => {
    const handle = document.createElement('div');
    handle.className = 'app-window-resize ' + cls;
    handle.addEventListener('mousedown', (e) => {
      if (win.classList.contains('fullscreen')) return;
      e.preventDefault(); e.stopPropagation();
      const rect = win.getBoundingClientRect();
      resizeState = { win, edge: cls, startX: e.clientX, startY: e.clientY,
        origX: rect.left, origY: rect.top, origW: rect.width, origH: rect.height };
      // Lock cursor on body during resize
      const curMap = {'r-right':'cur-ew','r-left':'cur-ew','r-top':'cur-ns','r-bottom':'cur-ns',
        'r-br':'cur-nwse','r-tl':'cur-nwse','r-bl':'cur-nesw','r-tr':'cur-nesw'};
      document.body.classList.add(curMap[cls] || 'cur-nwse');
    });
    win.appendChild(handle);
  });

  win.appendChild(titlebar);
  win.appendChild(body);
  document.body.appendChild(win);

  win.addEventListener('mousedown', () => bringToFront(win));

  titlebar.addEventListener('mousedown', (e) => {
    if (e.target.closest('.app-window-trafficlights')) return;
    if (win.classList.contains('fullscreen')) return;
    e.preventDefault();
    dragState = {
      win,
      startX: e.clientX - parseInt(win.style.left),
      startY: e.clientY - parseInt(win.style.top)
    };
    document.body.classList.add('cur-grab');
  });

  titlebar.addEventListener('dblclick', (e) => {
    if (e.target.closest('.app-window-trafficlights')) return;
    toggleFullscreen(win);
  });

  const entry = { appId, title, el: win, minimized: false };
  openWindows.push(entry);

  if (contentBuilder) contentBuilder(body, win);

  win.offsetHeight;
  win.classList.add('open');
  updateMenubarWindows();

  return win;
}

function closeAppWindow(el) {
  el.classList.remove('open', 'fullscreen', 'minimized');
  el.classList.add('closing');
  el.addEventListener('transitionend', () => {
    el.remove();
    const idx = openWindows.findIndex(w => w.el === el);
    if (idx >= 0) openWindows.splice(idx, 1);
    updateMenubarWindows();
  }, {once: true});
}

function minimizeAppWindow(win) {
  const entry = openWindows.find(w => w.el === win);
  if (!entry || entry.minimized) return;
  entry.minimized = true;
  updateMenubarWindows();
  const pill = document.querySelector(`.menubar-win-pill[data-app-id="${entry.appId}"]`);
  if (pill) {
    const pillRect = pill.getBoundingClientRect();
    const winRect = win.getBoundingClientRect();
    const targetX = pillRect.left + pillRect.width / 2 - winRect.width / 2;
    const targetY = pillRect.top;
    win.style.transform = `translate(${targetX - winRect.left}px, ${targetY - winRect.top}px) scale(0.08)`;
  }
  win.classList.add('minimized');
}

function restoreAppWindow(entry) {
  if (!entry.minimized) return;
  entry.minimized = false;
  entry.el.style.transition = 'opacity 0.25s ease-out, transform 0.3s cubic-bezier(0.2,0.8,0.3,1)';
  entry.el.style.transform = '';
  entry.el.classList.remove('minimized');
  bringToFront(entry.el);
  updateMenubarWindows();
  setTimeout(() => { entry.el.style.transition = ''; }, 350);
}

function toggleFullscreen(win) {
  const entry = openWindows.find(w => w.el === win);
  if (!entry) return;
  if (win.classList.contains('fullscreen')) {
    win.classList.remove('fullscreen');
    if (entry.savedRect) {
      win.style.left = entry.savedRect.left;
      win.style.top = entry.savedRect.top;
      win.style.width = entry.savedRect.width;
      win.style.height = entry.savedRect.height;
    }
  } else {
    entry.savedRect = {
      left: win.style.left, top: win.style.top,
      width: win.style.width, height: win.style.height
    };
    win.classList.add('fullscreen');
  }
}

// Global drag handlers
document.addEventListener('mousemove', (e) => {
  if (dragState) {
    let x = e.clientX - dragState.startX;
    let y = e.clientY - dragState.startY;
    const w = dragState.win.offsetWidth;
    x = Math.max(-w + 60, Math.min(window.innerWidth - 60, x));
    y = Math.max(28, Math.min(window.innerHeight - 38, y));
    dragState.win.style.left = x + 'px';
    dragState.win.style.top = y + 'px';
  }
  if (resizeState) {
    const rs = resizeState;
    const dx = e.clientX - rs.startX;
    const dy = e.clientY - rs.startY;
    let newX = rs.origX, newY = rs.origY, newW = rs.origW, newH = rs.origH;
    const edge = rs.edge;
    if (edge.includes('right') || edge === 'r-br' || edge === 'r-tr') newW = Math.max(320, rs.origW + dx);
    if (edge.includes('bottom') || edge === 'r-br' || edge === 'r-bl') newH = Math.max(200, rs.origH + dy);
    if (edge.includes('left') || edge === 'r-bl' || edge === 'r-tl') { newW = Math.max(320, rs.origW - dx); newX = rs.origX + rs.origW - newW; }
    if (edge.includes('top') || edge === 'r-tr' || edge === 'r-tl') { newH = Math.max(200, rs.origH - dy); newY = rs.origY + rs.origH - newH; }
    rs.win.style.left = newX + 'px'; rs.win.style.top = newY + 'px';
    rs.win.style.width = newW + 'px'; rs.win.style.height = newH + 'px';
  }
});
document.addEventListener('mouseup', () => {
  dragState = null; resizeState = null;
  document.body.classList.remove('cur-ew','cur-ns','cur-nwse','cur-nesw','cur-grab');
});

// ===== LAUNCH APP DISPATCHER =====
const APP_CONTENT_BUILDERS = {
  terminal:  { builder: buildTerminalContent,  w: 680, h: 440 },
  files:     { builder: buildFilesContent,     w: 720, h: 480 },
  browser:   { builder: buildBrowserContent,   w: 800, h: 540 },
  monitor:   { builder: buildMonitorContent,   w: 520, h: 440 },
  music:     { builder: buildMusicContent,     w: 340, h: 480 },
};

function launchApp(appId) {
  if (appId === 'settings') {
    openSettingsWindow();
    return;
  }
  const app = APP_CATALOG.find(a => a.id === appId);
  const label = app ? app.label : appId;
  const cfg = APP_CONTENT_BUILDERS[appId];
  if (cfg) {
    openAppWindow(appId, label, cfg.w, cfg.h, cfg.builder);
  } else {
    openAppWindow(appId, label, 520, 380, (body) => buildDefaultContent(body, app));
  }
}

// ===== APP CONTENT BUILDERS =====

function buildTerminalContent(body) {
  const term = document.createElement('div');
  term.className = 'app-terminal';

  const hl = (s) => '<span class="term-highlight">' + s + '</span>';
  const lines = [
    { prompt: true, path: '~', cmd: 'neofetch' },
    { output: hl('  _____                            ____  _____ ') },
    { output: hl(' |_   _|_ __ ___  _ __   ___  ___|  _ \\/ ____|') },
    { output: hl("   | | | '_ ` _ \\| '_ \\ / _ \\/ __| | | \\___ \\ ") },
    { output: hl('   | | | | | | | | |_) | (_) \\__ \\ |_| |___) |') },
    { output: hl('   |_| |_| |_| |_| .__/ \\___/|___/____/|____/ ') },
    { output: hl('                  |_|                          ') },
    { output: '' },
    { output: '  ' + hl('OS') + ':       ImposOS 0.1 i386' },
    { output: '  ' + hl('Kernel') + ':   imposkernel 0.1.0' },
    { output: '  ' + hl('Shell') + ':    impossh 1.0' },
    { output: '  ' + hl('WM') + ':       Liquid Glass Compositor' },
    { output: '  ' + hl('Terminal') + ': impos-term' },
    { output: '  ' + hl('CPU') + ':      i686 @ 120Hz PIT' },
    { output: '  ' + hl('Memory') + ':   64MB / 256MB (25%)' },
    { output: '  ' + hl('Disk') + ':     256MB imposfs v3' },
    { output: '' },
    { prompt: true, path: '~', cmd: 'ls /home' },
    { output: '<span style="color:#3478F6">Desktop</span>  <span style="color:#3478F6">Documents</span>  <span style="color:#3478F6">Downloads</span>  <span style="color:#3478F6">Music</span>  <span style="color:#3478F6">Pictures</span>' },
    { output: '' },
    { prompt: true, path: '~', cmd: '' },
  ];

  lines.forEach(l => {
    const div = document.createElement('div');
    div.className = 'term-line';
    if (l.prompt) {
      div.innerHTML = `<span class="term-prompt">root</span>@<span class="term-path">${l.path}</span> $ <span class="term-cmd">${l.cmd}</span>`;
      if (!l.cmd) {
        div.classList.add('term-input-line');
        div.innerHTML += '<span class="term-cursor"></span>';
      }
    } else {
      div.innerHTML = `<span class="term-output">${l.output}</span>`;
    }
    term.appendChild(div);
  });

  body.appendChild(term);
}

function buildFilesContent(body) {
  const wrap = document.createElement('div');
  wrap.className = 'app-files';

  const toolbar = document.createElement('div');
  toolbar.className = 'files-toolbar';
  toolbar.innerHTML = `
    <div class="files-breadcrumb">
      <span>/</span><span class="sep">/</span>
      <span>home</span><span class="sep">/</span>
      <span>root</span>
    </div>
  `;

  const grid = document.createElement('div');
  grid.className = 'files-grid';

  const items = [
    { name: 'Desktop',    folder: true },
    { name: 'Documents',  folder: true },
    { name: 'Downloads',  folder: true },
    { name: 'Music',      folder: true },
    { name: 'Pictures',   folder: true },
    { name: '.bashrc',    folder: false },
    { name: '.profile',   folder: false },
    { name: 'notes.txt',  folder: false },
    { name: 'todo.md',    folder: false },
    { name: 'config.sh',  folder: false },
    { name: 'build.sh',   folder: false },
    { name: 'kernel.elf', folder: false },
  ];

  const folderSvg = `<svg viewBox="0 0 36 36" fill="none"><path d="M3 10a2 2 0 012-2h8l3 3h13a2 2 0 012 2v14a2 2 0 01-2 2H5a2 2 0 01-2-2V10z" fill="rgba(52,199,89,0.25)" stroke="rgba(52,199,89,0.6)" stroke-width="1.3"/></svg>`;
  const fileSvg = `<svg viewBox="0 0 36 36" fill="none"><path d="M8 4h12l8 8v18a2 2 0 01-2 2H8a2 2 0 01-2-2V6a2 2 0 012-2z" fill="rgba(255,255,255,0.04)" stroke="rgba(255,255,255,0.2)" stroke-width="1.2"/><path d="M20 4v8h8" fill="none" stroke="rgba(255,255,255,0.15)" stroke-width="1.2"/></svg>`;

  items.forEach(item => {
    const el = document.createElement('div');
    el.className = 'files-item' + (item.folder ? ' is-folder' : '');
    el.innerHTML = (item.folder ? folderSvg : fileSvg) +
      `<span class="files-item-name">${item.name}</span>`;
    grid.appendChild(el);
  });

  const status = document.createElement('div');
  status.className = 'files-status';
  status.textContent = `${items.length} items — ${items.filter(i => i.folder).length} folders, ${items.filter(i => !i.folder).length} files`;

  wrap.appendChild(toolbar);
  wrap.appendChild(grid);
  wrap.appendChild(status);
  body.appendChild(wrap);
}

function buildBrowserContent(body) {
  const wrap = document.createElement('div');
  wrap.className = 'app-browser';

  const bar = document.createElement('div');
  bar.className = 'browser-bar';
  bar.innerHTML = `
    <div class="browser-nav">
      <button class="browser-nav-btn">\u2190</button>
      <button class="browser-nav-btn">\u2192</button>
      <button class="browser-nav-btn">\u21BB</button>
    </div>
    <input class="browser-url" type="text" value="impos://newtab" spellcheck="false">
  `;

  const content = document.createElement('div');
  content.className = 'browser-content';
  content.innerHTML = `
    <div class="browser-page">
      <h1>Welcome to ImposOS</h1>
      <p>The web browser for ImposOS. Built on the imposhttp networking stack with TCP/IP, DNS resolution, and HTML rendering.</p>
      <div class="browser-cards">
        <div class="browser-card">
          <h3>ImposOS Docs</h3>
          <p>Kernel documentation, syscall reference, and developer guides.</p>
        </div>
        <div class="browser-card">
          <h3>Network Tools</h3>
          <p>Ping, ARP tables, DNS lookup, and firewall configuration.</p>
        </div>
        <div class="browser-card">
          <h3>Package Manager</h3>
          <p>Browse and install packages from the ImposOS repository.</p>
        </div>
        <div class="browser-card">
          <h3>System Status</h3>
          <p>Real-time CPU, memory, disk, and network statistics.</p>
        </div>
      </div>
    </div>
  `;

  wrap.appendChild(bar);
  wrap.appendChild(content);
  body.appendChild(wrap);
}

function buildMonitorContent(body) {
  const wrap = document.createElement('div');
  wrap.className = 'app-monitor';

  // Resource bars
  const resources = [
    { label: 'CPU',    pct: 34, color: '#3478F6' },
    { label: 'Memory', pct: 62, color: '#34C759' },
    { label: 'Disk',   pct: 18, color: '#FF9500' },
    { label: 'Swap',   pct: 5,  color: '#AF52DE' },
  ];

  const resSection = document.createElement('div');
  const resLabel = document.createElement('div');
  resLabel.className = 'monitor-section-label';
  resLabel.textContent = 'Resources';
  resSection.appendChild(resLabel);

  resources.forEach(r => {
    const row = document.createElement('div');
    row.className = 'monitor-row';
    row.innerHTML = `
      <span class="monitor-label">${r.label}</span>
      <div class="monitor-bar"><div class="monitor-fill" style="width:${r.pct}%;background:${r.color}"></div></div>
      <span class="monitor-value">${r.pct}%</span>
    `;
    resSection.appendChild(row);
  });

  // Process list
  const procSection = document.createElement('div');
  procSection.style.cssText = 'flex:1;display:flex;flex-direction:column;min-height:0';
  const procLabel = document.createElement('div');
  procLabel.className = 'monitor-section-label';
  procLabel.textContent = 'Processes';
  procSection.appendChild(procLabel);

  const procHeader = document.createElement('div');
  procHeader.className = 'monitor-proc-header';
  procHeader.innerHTML = '<span>PID</span><span>Name</span><span>CPU</span><span>Mem</span>';
  procSection.appendChild(procHeader);

  const procs = [
    { pid: 1,  name: 'init',          cpu: '0.0%', mem: '0.8MB' },
    { pid: 2,  name: 'scheduler',     cpu: '0.2%', mem: '0.1MB' },
    { pid: 3,  name: 'desktop',       cpu: '8.4%', mem: '4.2MB' },
    { pid: 4,  name: 'wm',            cpu: '12.1%', mem: '6.8MB' },
    { pid: 5,  name: 'terminal',      cpu: '1.2%', mem: '1.4MB' },
    { pid: 6,  name: 'monitor',       cpu: '3.5%', mem: '2.1MB' },
    { pid: 7,  name: 'httpd',         cpu: '0.1%', mem: '0.6MB' },
    { pid: 8,  name: 'dhcp_client',   cpu: '0.0%', mem: '0.3MB' },
    { pid: 9,  name: 'kworker/0',     cpu: '0.4%', mem: '0.1MB' },
    { pid: 10, name: 'rtl8139_irq',   cpu: '0.3%', mem: '0.2MB' },
  ];

  const procList = document.createElement('div');
  procList.className = 'monitor-procs';
  procs.forEach(p => {
    const row = document.createElement('div');
    row.className = 'monitor-proc-row';
    row.innerHTML = `<span class="pid">${p.pid}</span><span>${p.name}</span><span class="cpu">${p.cpu}</span><span class="mem">${p.mem}</span>`;
    procList.appendChild(row);
  });
  procSection.appendChild(procList);

  wrap.appendChild(resSection);
  wrap.appendChild(procSection);
  body.appendChild(wrap);

  // Animate bars in
  setTimeout(() => {
    wrap.querySelectorAll('.monitor-fill').forEach(el => {
      const target = el.style.width;
      el.style.width = '0%';
      requestAnimationFrame(() => { el.style.width = target; });
    });
  }, 100);
}

function buildMusicContent(body) {
  const wrap = document.createElement('div');
  wrap.className = 'app-music';

  // Album art (generated on canvas)
  const artWrap = document.createElement('div');
  artWrap.className = 'music-art-wrap';
  const art = document.createElement('div');
  art.className = 'music-art';
  const artCanvas = document.createElement('canvas');
  artCanvas.width = 360; artCanvas.height = 360;
  const actx = artCanvas.getContext('2d');

  // Paint a simple gradient album art
  const g = actx.createLinearGradient(0, 0, 360, 360);
  g.addColorStop(0, '#1a0533');
  g.addColorStop(0.4, '#3a1c71');
  g.addColorStop(0.7, '#d76d77');
  g.addColorStop(1, '#ffaf7b');
  actx.fillStyle = g;
  actx.fillRect(0, 0, 360, 360);
  // Add some circles for texture
  for (let i = 0; i < 5; i++) {
    actx.beginPath();
    actx.arc(80 + i * 55, 180 + Math.sin(i) * 60, 30 + i * 8, 0, Math.PI * 2);
    actx.fillStyle = `rgba(255,255,255,${0.03 + i * 0.015})`;
    actx.fill();
  }

  art.appendChild(artCanvas);
  artWrap.appendChild(art);

  const info = document.createElement('div');
  info.className = 'music-info';
  info.innerHTML = `
    <div class="music-title">Kernel Panic</div>
    <div class="music-artist">The Interrupts</div>
  `;

  const progressWrap = document.createElement('div');
  progressWrap.className = 'music-progress-wrap';
  progressWrap.innerHTML = '<div class="music-progress"><div class="music-progress-fill"></div></div>';

  const time = document.createElement('div');
  time.className = 'music-time';
  time.innerHTML = '<span>1:24</span><span>4:07</span>';

  const controls = document.createElement('div');
  controls.className = 'music-controls';
  controls.innerHTML = `
    <button class="music-btn"><svg viewBox="0 0 16 16" width="14" height="14"><path d="M3 3h2v10H3zM7 8l7-5v10z" fill="white"/></svg></button>
    <button class="music-btn play"><svg viewBox="0 0 16 16" width="18" height="18"><polygon points="4,2 14,8 4,14" fill="white"/></svg></button>
    <button class="music-btn"><svg viewBox="0 0 16 16" width="14" height="14"><path d="M11 3h2v10h-2zM9 8L2 3v10z" fill="white"/></svg></button>
  `;

  wrap.appendChild(artWrap);
  wrap.appendChild(info);
  wrap.appendChild(progressWrap);
  wrap.appendChild(time);
  wrap.appendChild(controls);
  body.appendChild(wrap);
}

function buildDefaultContent(body, app) {
  const wrap = document.createElement('div');
  wrap.className = 'app-placeholder';
  const iconKey = app && app.icon ? app.icon : null;
  if (iconKey && ICONS[iconKey]) {
    const iconDiv = document.createElement('div');
    iconDiv.innerHTML = ICONS[iconKey];
    iconDiv.querySelector('svg').style.cssText = 'width:48px;height:48px;opacity:0.12';
    wrap.appendChild(iconDiv);
  } else {
    const placeholder = document.createElement('div');
    placeholder.innerHTML = `<svg viewBox="0 0 48 48" fill="none"><rect x="6" y="6" width="36" height="36" rx="8" stroke="white" stroke-width="2" opacity="0.12"/><line x1="18" y1="24" x2="30" y2="24" stroke="white" stroke-width="2" opacity="0.12"/></svg>`;
    wrap.appendChild(placeholder);
  }
  const label = document.createElement('div');
  label.className = 'placeholder-label';
  label.textContent = app ? `${app.label} is not yet implemented` : 'App not available';
  wrap.appendChild(label);
  body.appendChild(wrap);
}

// ===== SETTINGS WINDOW =====
function openSettingsWindow(initialTab) {
  const tab = initialTab || 'wallpaper';
  const win = openAppWindow('settings', 'Settings', 680, 440, (body, winEl) => {
    body.innerHTML = '';
    const layout = document.createElement('div');
    layout.className = 'settings-layout';

    const sidebar = document.createElement('div');
    sidebar.className = 'settings-sidebar';

    const tabs = [
      {id:'wallpaper', label:'Wallpaper',   icon:'<path d="M3 5h20v14H3z" fill="none" stroke="white" stroke-width="1.5"/><path d="M3 14l5-4 4 3 3-2 8 5" fill="none" stroke="white" stroke-width="1.3"/>'},
      {id:'appearance',label:'Appearance',  icon:'<circle cx="13" cy="13" r="8" fill="none" stroke="white" stroke-width="1.5"/><path d="M13 5v16M5 13h16" stroke="white" stroke-width="1.2"/>'},
      {id:'display',   label:'Display',     icon:'<rect x="3" y="5" width="20" height="13" rx="2" fill="none" stroke="white" stroke-width="1.5"/><line x1="13" y1="18" x2="13" y2="22" stroke="white" stroke-width="1.5"/><line x1="8" y1="22" x2="18" y2="22" stroke="white" stroke-width="1.5" stroke-linecap="round"/>'},
      {id:'about',     label:'About',       icon:'<circle cx="13" cy="13" r="9" fill="none" stroke="white" stroke-width="1.5"/><line x1="13" y1="12" x2="13" y2="18" stroke="white" stroke-width="1.8" stroke-linecap="round"/><circle cx="13" cy="8.5" r="1.2" fill="white"/>'},
    ];

    const content = document.createElement('div');
    content.className = 'settings-content';

    function selectTab(id) {
      sidebar.querySelectorAll('.settings-sidebar-item').forEach(s => s.classList.toggle('active', s.dataset.tab === id));
      content.innerHTML = '';
      if (id === 'wallpaper') buildWallpaperSettings(content);
      else if (id === 'about') buildAboutSettings(content);
      else {
        const ph = document.createElement('div');
        ph.className = 'settings-placeholder';
        ph.textContent = 'Coming soon';
        content.appendChild(ph);
      }
    }

    tabs.forEach(t => {
      const item = document.createElement('div');
      item.className = 'settings-sidebar-item';
      item.dataset.tab = t.id;
      item.innerHTML = `<svg viewBox="0 0 26 26" fill="none">${t.icon}</svg>${t.label}`;
      item.addEventListener('click', () => selectTab(t.id));
      sidebar.appendChild(item);
    });

    layout.appendChild(sidebar);
    layout.appendChild(content);
    body.appendChild(layout);

    selectTab(tab);
  });
  if (win && initialTab) {
    const item = win.querySelector(`.settings-sidebar-item[data-tab="${initialTab}"]`);
    if (item) item.click();
  }
}

function buildWallpaperSettings(container) {
  container.innerHTML = '<h2>Wallpaper</h2>';

  const grid = document.createElement('div');
  grid.className = 'wp-thumb-grid';

  const styleIds = Object.keys(WALLPAPER_STYLES);

  styleIds.forEach(id => {
    const style = WALLPAPER_STYLES[id];
    const card = document.createElement('div');
    card.className = 'wp-thumb' + (id === curStyleId ? ' active' : '');

    const cvs = document.createElement('canvas');
    cvs.width = 280; cvs.height = 174;
    const tctx = cvs.getContext('2d');
    const thIdx = (id === curStyleId) ? curThemeIdx : 0;
    style.draw(tctx, 280, 174, style.themes[thIdx], style.themes[thIdx], 1);

    const label = document.createElement('div');
    label.className = 'wp-thumb-label';
    label.textContent = style.label;

    card.appendChild(cvs);
    card.appendChild(label);

    card.addEventListener('click', () => {
      setWallpaperStyle(id, 0);
      buildWallpaperSettings(container);
    });

    grid.appendChild(card);
  });

  container.appendChild(grid);

  const style = getCurStyle();
  if (style.themes.length > 1) {
    const varRow = document.createElement('div');
    varRow.className = 'wp-variants';

    const varLabel = document.createElement('span');
    varLabel.className = 'wp-variant-label';
    varLabel.textContent = 'Theme:';
    varRow.appendChild(varLabel);

    style.themes.forEach((th, i) => {
      const dot = document.createElement('div');
      dot.className = 'wp-variant-dot' + (i === curThemeIdx ? ' active' : '');
      dot.style.background = th.dot || th.sky?.[2] || '#444';
      dot.title = th.name;
      dot.addEventListener('click', () => {
        setWallpaperStyle(curStyleId, i);
        buildWallpaperSettings(container);
      });
      varRow.appendChild(dot);
    });

    const nameLbl = document.createElement('span');
    nameLbl.className = 'wp-variant-label';
    nameLbl.textContent = style.themes[curThemeIdx].name;
    varRow.appendChild(nameLbl);

    container.appendChild(varRow);
  }
}

function buildAboutSettings(container) {
  container.innerHTML = '';

  const header = document.createElement('div');
  header.className = 'about-header';
  header.innerHTML = `
    <div class="about-logo">iO</div>
    <div class="about-title-group">
      <h1>ImposOS</h1>
      <div class="about-version">Version 0.1.0 &mdash; Liquid Glass Edition</div>
    </div>
  `;
  container.appendChild(header);

  const stats = document.createElement('div');
  stats.className = 'about-stats';
  const statData = [
    { label: 'Architecture', value: 'i386 (32-bit)' },
    { label: 'Kernel',       value: 'imposkernel 0.1' },
    { label: 'Filesystem',   value: 'imposfs v3' },
    { label: 'Memory',       value: '256 MB' },
    { label: 'Display',      value: '1920\u00d71080 @ 32bpp' },
    { label: 'Network',      value: 'TCP/IP Stack' },
  ];
  statData.forEach(s => {
    const el = document.createElement('div');
    el.className = 'about-stat';
    el.innerHTML = `<div class="stat-label">${s.label}</div><div class="stat-value">${s.value}</div>`;
    stats.appendChild(el);
  });
  container.appendChild(stats);

  const credits = document.createElement('div');
  credits.className = 'about-credits';
  credits.innerHTML = `A bare-metal operating system built from scratch.<br>
    Multiboot-compliant kernel &bull; Custom filesystem with journaling &bull; Full networking stack<br>
    Window manager with liquid glass compositing &bull; 42+ shell commands`;
  container.appendChild(credits);
}
