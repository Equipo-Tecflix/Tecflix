import { useEffect, useState } from 'react';

interface Video {
  ID: string;
  Titulo: string;
  Autor: string;
  Genero: string;
  Duracion: string;
}

function App() {
  const [usuario, setUsuario] = useState<string | null>(null);
  const [correo, setCorreo] = useState('');
  const [clave, setClave] = useState('');
  const [registro, setRegistro] = useState(false);

  const [videos, setVideos] = useState<Video[]>([]);
  const [orden, setOrden] = useState<'Titulo' | 'Autor'>('Titulo');
  const [pagina, setPagina] = useState(1);
  const porPagina = 2;

  const [titulo, setTitulo] = useState('');
  const [autor, setAutor] = useState('');
  const [genero, setGenero] = useState('');
  const [duracion, setDuracion] = useState('');

  const [viendo, setViendo] = useState<Video | null>(null);

  useEffect(() => {
    fetch('/BasedeDatos_Videos.json')
      .then((res) => res.json())
      .then((data) => {
        const lista: Video[] = Object.values(data).map((v: any) =>
          typeof v === 'string' ? JSON.parse(v) : v
        );
        setVideos(lista);
      })
      .catch((err) => console.error('Error cargando JSON:', err));
  }, []);

  const entrar = (e: React.FormEvent) => {
    e.preventDefault();
    if (correo && clave) setUsuario(correo);
  };

  const agregar = (e: React.FormEvent) => {
    e.preventDefault();
    if (!titulo || !autor) return;
    const nuevo: Video = {
      ID: Math.floor(100000 + Math.random() * 900000).toString(),
      Titulo: titulo,
      Autor: autor,
      Genero: genero || 'General',
      Duracion: duracion || '1h 30m',
    };
    setVideos([...videos, nuevo]);
    setTitulo('');
    setAutor('');
    setGenero('');
    setDuracion('');
  };

  const ordenados = [...videos].sort((a, b) => a[orden].localeCompare(b[orden]));
  const totalPaginas = Math.ceil(ordenados.length / porPagina) || 1;
  const visibles = ordenados.slice((pagina - 1) * porPagina, pagina * porPagina);

  if (!usuario) {
    return (
      <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', minHeight: '100vh', backgroundColor: '#141414', color: 'white', fontFamily: 'Arial' }}>
        <form onSubmit={entrar} style={{ backgroundColor: '#1f1f1f', padding: '35px', borderRadius: '8px', width: '340px', borderTop: '4px solid #E50914' }}>
          <h1 style={{ color: '#E50914', textAlign: 'center', margin: '0 0 10px 0' }}>TECFLIX</h1>
          <h3 style={{ textAlign: 'center', color: '#ccc', marginBottom: '20px' }}>{registro ? 'Crear Cuenta' : 'Iniciar Sesión'}</h3>
          <input type="email" placeholder="Correo (@estudiantec.cr)" value={correo} onChange={(e) => setCorreo(e.target.value)} required style={{ width: '100%', padding: '10px', marginBottom: '12px', backgroundColor: '#333', border: 'none', color: 'white', borderRadius: '4px', boxSizing: 'border-box' }} />
          <input type="password" placeholder="Contraseña" value={clave} onChange={(e) => setClave(e.target.value)} required style={{ width: '100%', padding: '10px', marginBottom: '18px', backgroundColor: '#333', border: 'none', color: 'white', borderRadius: '4px', boxSizing: 'border-box' }} />
          <button type="submit" style={{ width: '100%', padding: '10px', backgroundColor: '#E50914', color: 'white', border: 'none', borderRadius: '4px', fontWeight: 'bold', cursor: 'pointer' }}>
            {registro ? 'Registrarse' : 'Entrar'}
          </button>
          <p onClick={() => setRegistro(!registro)} style={{ textAlign: 'center', marginTop: '14px', color: '#aaa', cursor: 'pointer', fontSize: '13px' }}>
            {registro ? '¿Ya tenés cuenta? Iniciá sesión' : '¿Sin cuenta? Registrate aquí'}
          </p>
        </form>
      </div>
    );
  }

  return (
    <div style={{ padding: '30px', fontFamily: 'Arial', backgroundColor: '#141414', color: 'white', minHeight: '100vh' }}>
      <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '2px solid #E50914', paddingBottom: '12px', marginBottom: '25px' }}>
        <h1 style={{ color: '#E50914', margin: 0 }}>TECFLIX</h1>
        <div>
          <span style={{ color: '#00df89', marginRight: '15px', fontSize: '14px' }}>● {usuario}</span>
          <button onClick={() => setUsuario(null)} style={{ padding: '6px 12px', backgroundColor: '#333', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}>Salir</button>
        </div>
      </header>

      {viendo && (
        <div style={{ backgroundColor: '#000', padding: '15px', borderRadius: '6px', marginBottom: '25px', border: '1px solid #E50914' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '10px' }}>
            <strong style={{ color: '#E50914' }}>▶ {viendo.Titulo} - {viendo.Autor}</strong>
            <button onClick={() => setViendo(null)} style={{ backgroundColor: '#E50914', color: 'white', border: 'none', padding: '4px 10px', borderRadius: '4px', cursor: 'pointer' }}>Cerrar ✕</button>
          </div>
          <video controls autoPlay style={{ width: '100%', maxHeight: '350px', backgroundColor: '#111' }}>
            <source src={`http://127.0.0{viendo.ID}`} type="video/mp4" />
          </video>
        </div>
      )}

      <form onSubmit={agregar} style={{ display: 'flex', gap: '8px', flexWrap: 'wrap', backgroundColor: '#1f1f1f', padding: '15px', borderRadius: '6px', marginBottom: '20px' }}>
        <input placeholder="Título" value={titulo} onChange={(e) => setTitulo(e.target.value)} required style={{ padding: '8px', backgroundColor: '#333', border: 'none', color: 'white', borderRadius: '4px', flex: 1 }} />
        <input placeholder="Autor" value={autor} onChange={(e) => setAutor(e.target.value)} required style={{ padding: '8px', backgroundColor: '#333', border: 'none', color: 'white', borderRadius: '4px', flex: 1 }} />
        <input placeholder="Género" value={genero} onChange={(e) => setGenero(e.target.value)} style={{ padding: '8px', backgroundColor: '#333', border: 'none', color: 'white', borderRadius: '4px', width: '130px' }} />
        <input placeholder="Duración" value={duracion} onChange={(e) => setDuracion(e.target.value)} style={{ padding: '8px', backgroundColor: '#333', border: 'none', color: 'white', borderRadius: '4px', width: '110px' }} />
        <button type="submit" style={{ padding: '8px 15px', backgroundColor: '#E50914', color: 'white', border: 'none', borderRadius: '4px', fontWeight: 'bold', cursor: 'pointer' }}>+ Agregar</button>
      </form>

      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
        <h3 style={{ margin: 0 }}>Catálogo ({videos.length})</h3>
        <div>
          <button onClick={() => setOrden('Titulo')} style={{ padding: '6px 10px', marginRight: '6px', backgroundColor: orden === 'Titulo' ? '#E50914' : '#333', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}>Ordenar: Título</button>
          <button onClick={() => setOrden('Autor')} style={{ padding: '6px 10px', backgroundColor: orden === 'Autor' ? '#E50914' : '#333', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}>Ordenar: Autor</button>
        </div>
      </div>

      <table style={{ width: '100%', borderCollapse: 'collapse', backgroundColor: '#181818' }}>
        <thead>
          <tr style={{ borderBottom: '2px solid #333', textAlign: 'left', color: '#E50914' }}>
            <th style={{ padding: '10px' }}>ID</th>
            <th style={{ padding: '10px' }}>Título</th>
            <th style={{ padding: '10px' }}>Autor</th>
            <th style={{ padding: '10px' }}>Género</th>
            <th style={{ padding: '10px' }}>Duración</th>
            <th style={{ padding: '10px', textAlign: 'center' }}>Ver</th>
          </tr>
        </thead>
        <tbody>
          {visibles.map((v) => (
            <tr key={v.ID} style={{ borderBottom: '1px solid #222' }}>
              <td style={{ padding: '10px', color: '#888' }}>{v.ID}</td>
              <td style={{ padding: '10px', fontWeight: 'bold' }}>{v.Titulo}</td>
              <td style={{ padding: '10px' }}>{v.Autor}</td>
              <td style={{ padding: '10px', color: '#aaa' }}>{v.Genero}</td>
              <td style={{ padding: '10px', color: '#00df89' }}>{v.Duracion}</td>
              <td style={{ padding: '10px', textAlign: 'center' }}>
                <button onClick={() => setViendo(v)} style={{ padding: '5px 12px', backgroundColor: '#00df89', color: '#000', border: 'none', borderRadius: '4px', fontWeight: 'bold', cursor: 'pointer' }}>▶</button>
              </td>
            </tr>
          ))}
        </tbody>
      </table>

      <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', gap: '12px', marginTop: '18px' }}>
        <button disabled={pagina === 1} onClick={() => setPagina(pagina - 1)} style={{ padding: '6px 12px', backgroundColor: '#333', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}>← Anterior</button>
        <span>Página {pagina} de {totalPaginas}</span>
        <button disabled={pagina >= totalPaginas} onClick={() => setPagina(pagina + 1)} style={{ padding: '6px 12px', backgroundColor: '#333', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}>Siguiente →</button>
      </div>
    </div>
  );
}

export default App;
