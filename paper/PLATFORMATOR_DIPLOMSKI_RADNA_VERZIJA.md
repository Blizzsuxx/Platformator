# Platformator: Radna Verzija Diplomskog Rada

## Podaci za popunjavanje

- Autor: [Ime i prezime]
- Broj indeksa: [SW xx/20xx]
- Mentor: [ime i prezime, zvanje]
- Datum prihvatanja teme: [dd.mm.gggg.]
- Datum odbrane: [dd.mm.gggg.]
- Godina predaje: [2026]
- Konačan naslov rada: Razvoj 2D gejm endžina sa editorom scena i optimizovanom detekcijom kolizija zasnovanom na segmentisanoj sweep-and-prune metodi

## Naslov rada

Razvoj 2D gejm endžina sa editorom scena i optimizovanom detekcijom kolizija zasnovanom na segmentisanoj sweep-and-prune metodi

## Izvod

U radu je predstavljen razvoj sistema Platformator, 2D gejm endžina sa pratećim desktop editorom scena i fizičkim podsistemom za detekciju i rešavanje kolizija. Jezgro sistema implementirano je u programskom jeziku C++20 uz korišćenje biblioteka SDL3, Eigen, oneTBB i nlohmann/json. Poseban fokus rada stavljen je na broad-phase detekciju kolizija zasnovanu na grid-subdiviziji prostora, AABB strukturama i segmentisanim intervalnim listama inspirisanim radom Tracy, Buss i Woods. Pored runtime dela, razvijen je i editor scena nalik na Unity tok rada, koji omogućava upravljanje hijerarhijom objekata, komponentama, resursima i pokretanjem scena. Funkcionalnost sistema proverena je automatizovanim testovima i demonstracionim primerom platformske igre. Dobijeni rezultati pokazuju da Platformator uspešno objedinjuje istraživačku ideju optimizovane kolizione detekcije i praktično razvojno okruženje za 2D igre.

## Abstract

This thesis presents the development of Platformator, a 2D game engine accompanied by a desktop scene editor and a physics subsystem for collision detection and resolution. The core of the system is implemented in C++20 using SDL3, Eigen, oneTBB and nlohmann/json. The main focus is placed on the broad-phase collision detection subsystem based on spatial grid subdivision, AABB structures and segmented interval lists inspired by the method of Tracy, Buss and Woods. In addition to the runtime core, a Unity-like scene editor was developed to support hierarchy browsing, component inspection, asset management and scene execution. The system was validated through automated tests and a Mario-like platformer example. The results indicate that Platformator successfully combines a research-inspired collision detection approach with a practical development environment for 2D games.

## 1. Uvod

Razvoj gejm endžina predstavlja spoj više softverskih disciplina: računarske grafike, fizičke simulacije, upravljanja resursima, dizajna alata i arhitekture softverskih sistema. Iako su komercijalni i open-source endžini danas široko dostupni, razvoj sopstvenog rešenja ostaje značajan inženjerski i istraživački zadatak, naročito kada je cilj da se određeni algoritamski pristup ne samo implementira, već i integriše u koherentno razvojno okruženje. U tom kontekstu, ovaj rad se bavi razvojem 2D gejm endžina Platformator, sa posebnim osvrtom na efikasnu detekciju kolizija i podršku za uređivanje scena.

Motivacija rada proističe iz potrebe da se istraživački koncepti iz oblasti broad-phase detekcije kolizija pretoče u praktičan sistem koji je moguće koristiti za razvoj manjih 2D igara. Problem koji se razmatra nije samo kako efikasno otkriti potencijalne kolizione parove u sceni sa većim brojem objekata, već i kako takav fizički podsistem uklopiti u širu arhitekturu koja obuhvata komponente, scene, renderovanje, audio i korisnički editor. Time se rad pozicionira između istraživačkog prototipa i upotrebljivog inženjerskog proizvoda.

Cilj rada je razvoj funkcionalnog 2D gejm endžina zasnovanog na komponentnoj arhitekturi, uz implementaciju broad-phase detekcije kolizija inspirisane segmentisanom sweep-and-prune metodom. Pored runtime jezgra, cilj je i razvoj editora scena koji omogućava rad sa hijerarhijom objekata, uređivanje komponenti, pregled resursa i pokretanje scene iz istog okruženja. Poseban cilj je i demonstracija upotrebljivosti sistema kroz konkretan primer 2D platformske igre.

Doprinos rada može se sagledati kroz tri celine. Prva celina je arhitekturalni doprinos, jer je razvijen komponentno organizovan sistem koji objedinjuje scene, objekte, fiziku, skriptovanje, audio i vizuelni prikaz. Druga celina je algoritamski doprinos, jer je broad-phase podsistem zasnovan na grid-subdiviziji, AABB strukturama i segmentisanim intervalnim listama koje koriste lokalno sortirane segmente i checkpoint skupove. Treća celina je alatni doprinos, jer je uz runtime razvijen i zaseban desktop editor koji značajno podiže upotrebljivost celog rešenja.

Struktura rada organizovana je tako da najpre predstavi stanje u oblasti i teorijske osnove, zatim arhitekturu i implementaciju sistema Platformator, a nakon toga evaluaciju i demonstraciju rezultata. Na kraju su dati zaključci, ograničenja i pravci budućeg razvoja.

## 2. Pregled stanja u oblasti

Razvoj gejm endžina oslanja se i na industrijsku praksu i na rezultate akademskih istraživanja. U oblasti detekcije kolizija, posebno su značajni radovi koji se bave broad-phase filtriranjem velikog broja potencijalnih sudara. Među njima se izdvajaju pristupi zasnovani na sweep-and-prune logici i inkrementalnom održavanju intervala, kao i sistemi koji kombinuju prostornu subdiviziju sa efikasnim lokalnim strukturama podataka.

Rad Tracy, Buss i Woods predstavlja centralnu teorijsku inspiraciju za ovaj rad, jer uvodi segmentisane intervalne liste kao način da se insertion i removal događaji obrađuju efikasnije od tradicionalnog monolitnog sweep-and-prune pristupa. Ideja je posebno pogodna za scene u kojima postoji značajna vremenska koherentnost, odnosno gde se veliki deo objekata između susednih frejmova menja relativno malo.

Sa druge strane, savremeni gejm endžini kao što su Unity i Godot popularizovali su komponentni model, scene kao osnovnu organizacionu jedinicu i vizuelne editore sa hijerarhijskim pregledom i inspektorom svojstava. Ti sistemi predstavljaju važan uzor za organizaciju korisničkog toka rada, iako ovaj rad ne pokušava da ponovi njihov puni obim funkcionalnosti.

Na osnovu pregleda stanja u oblasti može se zaključiti da postoje dva pravca koja je potrebno povezati. Prvi je algoritamski pravac, usmeren na efikasnu detekciju kolizija. Drugi je inženjerski pravac, usmeren na razvoj koherentnog alata za kreiranje i izvođenje scena. Platformator nastaje upravo na preseku ta dva pravca.

## 3. Teorijske osnove

Komponentna arhitektura zasniva se na ideji da se ponašanje objekata gradi kompozicijom manjih funkcionalnih jedinica umesto dubokim hijerarhijama nasleđivanja. U okviru ovog rada, GameObject predstavlja osnovni entitet scene, dok se konkretne sposobnosti dodaju kroz komponente kao što su Sprite, Collider, Rigidbody, Audio, Animator, Camera i ScriptComponent. Ovakav pristup povećava fleksibilnost sistema i olakšava njegovo povezivanje sa editorom scena.

Broad-phase detekcija kolizija zasniva se na eliminaciji očigledno nepovezanih parova objekata pre primene skupljih geometrijskih provera. U ovom radu korišćeni su AABB omotači, grid-subdivizija prostora i segmentisane intervalne liste za održavanje projekcija objekata po osama. Ključna ideja je da se potencijalni parovi prepoznaju što ranije i što lokalnije, uz iskorišćavanje vremenske koherentnosti scene.

Za narrow-phase proveru i određivanje kontakata koristi se SAT pristup za konveksne 2D oblike. Na osnovu projekcija na relevantne ose moguće je utvrditi da li postoji sudar, kao i proceniti normalu kontakta i dubinu penetracije. Dobijeni podaci se zatim koriste u impulsnom solveru za rešavanje kontakata između rigidbody objekata.

## 4. Projektovanje i implementacija sistema

Platformator je projektovan kao 2D gejm endžin sa odvojenim runtime i editor slojem. Runtime jezgro implementirano je u C++20 i odgovorno je za upravljanje scenama, objektima, komponentama, fizičkom simulacijom, renderovanjem i audio obradom. Editor je implementiran u Python-u uz PySide6 i obezbeđuje korisnički interfejs za pregled i uređivanje scena.

U okviru runtime sloja centralnu ulogu ima GameManager, koji upravlja glavnom petljom aplikacije i koordinira rad svih podsistema. GameObject objekti sadrže transformaciju, hijerarhijske odnose i skup komponenti. Na ovaj način fizički, vizuelni i logički aspekti scene ostaju povezani, ali i dovoljno modularni da se njima može upravljati kroz editor i skripte.

Fizički podsistem organizovan je oko PhysicsManager komponente. Prostor scene deli se na grid ćelije, pri čemu svaka ćelija održava lokalnu AABB strukturu sa X i Y projekcijama. Segmentisane intervalne liste služe za efikasno inkrementalno održavanje projekcija, a checkpoint skupovi omogućavaju da se lokalno zadrži informacija o intervalima koji prelaze granice segmenta. Na nivou celog sistema witness counting mehanizam sprečava gubitak informacija o preklapanju kod objekata koji obuhvataju više ćelija.

Nakon broad-phase faze, kandidat-parovi se proveravaju u narrow-phase fazi pomoću SAT algoritma, a potom se primenjuje impulsni solver koji rešava brzine i kontakte. Implementacija podržava dinamička, statička i kinematička tela, restituciju, trenje i mehanizam uspavljivanja rigidbody objekata.

Editor scena predstavlja zaseban, ali usko povezan deo sistema. Njegov interfejs obuhvata panel hijerarhije objekata, inspector komponenti, pregled resursa, prikaz dostupnih ponašanja i izlazni panel za build i run komande. Ovakav alatni sloj približava tok rada onome koji je poznat iz Unity i sličnih okruženja.

Kao demonstracija upotrebljivosti razvijen je Mario primer. On koristi javni runtime interfejs, sopstvene skripte ponašanja i posebne resurse, čime pokazuje da je Platformator upotrebljiv i izvan internog testnog koda.

## 5. Evaluacija i rezultati

Evaluacija sistema sprovedena je kroz kombinaciju automatizovanih testova, ručnih funkcionalnih scenarija i demonstracionog primera. Ovakav pristup je izabran zato što gejm endžin nije moguće adekvatno proceniti samo jednim tipom provere. Potrebno je istovremeno proveriti korektnost implementacije, stabilnost integracije podsistema i praktičnu upotrebljivost u radu sa scenama.

Automatizovani deo evaluacije obuhvata regresione i specijalizovane test programe. U njih spadaju provere broad-phase logike, checkpoint mehanizma, rada sa scenama i skriptama, bezbednog brisanja objekata i rekreacije runtime okruženja. Na osnovu sprovedene validacije, projektno definisani test izvršni programi prolaze uspešno, što potvrđuje funkcionalnu stabilnost ključnih podsistema.

Ručni funkcionalni scenariji obuhvataju rad u editoru, uključujući učitavanje scene, izmenu objekata i komponenti, čuvanje izmena i pokretanje scene iz istog alata. Rezultati pokazuju da je osnovni tok rada funkcionalan i dovoljno stabilan za demonstraciju i dalje proširenje.

Pored funkcionalne validacije, završna verzija rada treba da uključi i posebnu kvantitativnu evaluaciju performansi broad-phase podsistema. Ta evaluacija treba da obuhvati sintetičke scene sa različitim brojem kolajdera, različitim udelom pokretnih objekata i različitim brojem insercija i uklanjanja po frejmu. Na taj način će se doprinos segmentisanih intervalnih listi potkrepiti i numeričkim podacima.

## 6. Demonstracija sistema

Demonstracija implementiranog sistema može se organizovati kroz tri povezana nivoa. Prvi nivo obuhvata rad u editoru scena, pri čemu se komisiji može prikazati otvaranje scene, izbor objekta u hijerarhiji, izmena svojstava i ponovno pokretanje scene. Drugi nivo obuhvata runtime prikaz jednostavne scene sa fizičkom interakcijom, animacijom i audio sadržajem. Treći nivo obuhvata Mario primer, koji predstavlja najkompletniju demonstraciju funkcionalnosti sistema.

Ovako organizovana demonstracija ima praktičnu vrednost, jer istovremeno prikazuje alatni deo rada, fizički podsistem i završnu upotrebljivost endžina u kontekstu konkretne igre. To je posebno važno za komisiju, pošto pokazuje da je sistem zaista koherentan, a ne samo skup izolovanih tehničkih modula.

## 7. Zaključak

Platformator predstavlja funkcionalan prototip 2D gejm endžina koji uspešno objedinjuje komponentnu arhitekturu, fizički podsistem, rad sa scenama i editor za kreiranje sadržaja. Poseban značaj rada ogleda se u tome što je algoritamska ideja segmentisane sweep-and-prune broad-phase obrade integrisana u praktičan razvojni sistem, a ne posmatrana izolovano.

Rezultati pokazuju da razvijeno rešenje ispunjava osnovne ciljeve rada: podržava organizaciju scene kroz objekte i komponente, omogućava broad-phase i narrow-phase obradu kolizija, obezbeđuje vizuelni editor i demonstrira upotrebljivost kroz Mario primer. Time je postignuta ravnoteža između istraživačkog i inženjerskog aspekta zadatka.

Istovremeno, sistem ostavlja jasan prostor za dalji razvoj. Budući rad treba da obuhvati detaljnije benchmark merenje broad-phase performansi, podršku za kontinuiranu detekciju kolizija za veoma brze objekte, dodatna poboljšanja redosleda fizičke obrade i dalje unapređenje editora. Uprkos tim otvorenim tačkama, Platformator već sada predstavlja čvrstu osnovu za dalji istraživački i praktični razvoj.

## Literatura

[1] Tracy, D. J., Buss, S. R., Woods, B. M. Efficient Large-Scale Sweep and Prune Methods with AABB Insertion and Removal. IEEE Virtual Reality Conference, 2009.

[2] Baraff, D. Dynamic Simulation of Non-Penetrating Rigid Bodies. Cornell University, 1992.

[3] Cohen, J. D., Lin, M. C., Manocha, D., Ponamgi, M. K. I-COLLIDE: An Interactive and Exact Collision Detection System for Large-Scale Environments. Symposium on Interactive 3D Graphics, 1995.

[4] van den Bergen, G. Collision Detection in Interactive 3D Environments. Morgan Kaufmann, 2003.

[5] Unity Technologies. Unity Manual. https://docs.unity3d.com/Manual/

[6] Godot Engine. Official Documentation. https://docs.godotengine.org/

[7] SDL. SDL3 Documentation and Wiki. https://wiki.libsdl.org/SDL3

[8] Platformator source code repository. [uneti URL ako bude potreban].

## Biografija

[Ime i prezime kandidata] je rođen/a [dd.mm.gggg.] godine u [mesto]. Osnovno i srednje obrazovanje završio/la je u [mesto]. Studije na Fakultetu tehničkih nauka u Novom Sadu, na studijskom programu Softversko inženjerstvo i informacione tehnologije, upisao/la je školske [20xx/20xx] godine. Položio/la je sve ispite predviđene studijskim programom i stekao/la uslov za odbranu diplomskog rada.

## Ključna dokumentacijska informacija

- Autor: [Ime i prezime kandidata]
- Mentor: [ime i prezime mentora, zvanje]
- Naslov: Razvoj 2D gejm endžina sa editorom scena i optimizovanom detekcijom kolizija zasnovanom na segmentisanoj sweep-and-prune metodi
- Jezik rada: srpski
- Ključne reči: gejm endžin, detekcija kolizija, sweep and prune, editor scena, komponentna arhitektura
- Godina: [2026]

## Key words documentation

- Author: [Candidate full name]
- Mentor: [Mentor full name, title]
- Title: Development of a 2D Game Engine with a Scene Editor and Optimized Collision Detection Based on the Segmented Sweep-and-Prune Method
- Language: Serbian
- Keywords: game engine, collision detection, sweep and prune, scene editor, component-based architecture
- Year: [2026]

## Checklista pre predaje

1. Popuniti sve lične i administrativne podatke.
2. Uskladiti naslov sa zvanično odobrenom prijavom teme.
3. Dopuniti benchmark tabelama i numeričkim rezultatima.
4. Proveriti bibliografski stil koji traži mentor.
5. Prebaciti sadržaj u konačni FTN Word ili LaTeX šablon.