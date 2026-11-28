#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm> // Librería oficial para ordenar vectores (std::sort)
#include "json.hpp" 
#include "Video.hpp" //Cosita de videos 

using json = nlohmann::json;

// Función de Paginación: Recibe todo el catálogo, la página que queremos y cuántos videos por página
std::vector<Video> paginarVideos(const std::vector<Video>& todos, int pagina, int tamanoPagina) {
    std::vector<Video> paginaActual;
    int inicio = (pagina - 1) * tamanoPagina;
    int fin = std::min(inicio + tamanoPagina, (int)todos.size());

    for (int i = inicio; i < fin; i++) {
        paginaActual.push_back(todos[i]);
    }
    return paginaActual;
}

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "     TECFLIX METADATA SERVER - VERSION FINAL 100%        " << std::endl;
    std::cout << "=========================================================" << std::endl;

    // Ejemplo con objetos video reales
    Video V1("Cars 3", "Disney", "Infantil", "1h2m", 80008);
    Video V2("Dragon Ball", "Goku", "Anime", "2h2m", 123456778);
    Video V3("Spider-Man: Brand New Day", "Daniel Creston", "Superheroes", "2h25m", 20260730);
    Video V4("Avatar: El Camino del Agua", "James Cameron", "Ciencia Ficcion", "3h12m", 99887766);

    // Los vectores son como un tunel para hacer listas/arreglos, algo así.
    std::vector<Video> listavideos = {V1, V2, V3, V4};

    // 1. ORDENAMIENTO (Requisito de rúbrica): Ordenamos alfabéticamente por Título usando to_json()
    std::sort(listavideos.begin(), listavideos.end(), [](Video a, Video b) {
        return a.to_json()["Titulo"] < b.to_json()["Titulo"];
    });
    std::cout << "[INFO] Catalogo ordenado alfabeticamente por Titulo." << std::endl;

    // 2. PAGINACIÓN (Requisito de rúbrica): Exportamos catálogo completo y demostramos paginación
    std::vector<Video> primeraPagina = paginarVideos(listavideos, 1, 2);
    std::cout << "[INFO] Paginacion activa: Mostrando " << primeraPagina.size() << " videos en pagina 1." << std::endl;

    // Es para registrar los videos y que se vean reflejados en un json
    // Nota del junior (Yo): Endpoint es como decir "Si alguien viene por el pasillo de internet y busca la puerta de /videos, ejecute TODO el código de abajo."
    json listajsonoutput;
    for(int i = 0; i < listavideos.size(); i++){
        listajsonoutput[std::to_string(i)] = listavideos[i].to_json().dump(4);
    }

    std::string ruta_salida = "../BasedeDatos_Videos.json";
    std::ofstream fileout(ruta_salida);

    if(fileout.is_open()){
        fileout << listajsonoutput.dump(4);
        fileout.close(); //el archivo siempre se debe cerrar para guardar los cambios;
        std::cout << "[EXITO] Archivo maestro actualizado en: " << ruta_salida << std::endl;
    } else {
        std::cerr << "Ha ocurrido un error a la hora de guardar el archivo." << std::endl;
    }

    return 0;
}
