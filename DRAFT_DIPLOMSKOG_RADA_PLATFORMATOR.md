# Nacrt Diplomskog Rada Za Platformator

> Napomena: Ovaj dokument predstavlja radnu verziju diplomskog rada prilagodjenu projektu Platformator. Tekst je namerno napisan tako da moze da posluzi kao polazna verzija rada u formatu slicnom primeru koji je dat od strane mentora ili profesora. Mesta oznacena sa `<...>` ili `[dopuniti]` treba popuniti stvarnim podacima o kandidatu, mentoru, datumu, komisiji, merenjima i finalnim komentarima nakon sto benchmark rezultati budu normalizovani.

---

UNIVERZITET U NOVOM SADU  
FAKULTET TEHNICKIH NAUKA U NOVOM SADU

<Ime i prezime kandidata>

RAZVOJ 2D GEJM ENDZINA SA EDITOROM SCENA I OPTIMIZOVANOM BROAD-PHASE DETEKCIJOM KOLIZIJA ZASNOVANOM NA SEGMENTISANOJ SWEEP-AND-PRUNE METODI

Diplomski rad  
- Osnovne akademske studije -

Novi Sad, 2026.

---

UNIVERZITET U NOVOM SADU  
FAKULTET TEHNICKIH NAUKA  
21000 NOVI SAD, Trg Dositeja Obradovica 6

Datum: <datum>

ZADATAK ZA IZRADU DIPLOMSKOG (BACHELOR) RADA  
List: 1/1

Vrsta studija: Osnovne akademske studije  
Studijski program: Softversko inzenjerstvo i informacione tehnologije  
Rukovodilac studijskog programa: prof. dr Miroslav Zaric

Student: <Ime i prezime>  
Broj indeksa: SW xx/20xx

Oblast: Racunarska grafika i interaktivni sistemi  
Mentor: dr <Ime i prezime>, <zvanje>

NA OSNOVU PODNETE PRIJAVE, PRILOZENE DOKUMENTACIJE I ODREDBI STATUTA FAKULTETA IZDAJE SE ZADATAK ZA DIPLOMSKI RAD, SA SLEDECIM ELEMENTIMA:

- problem - tema rada;
- nacin resavanja problema i nacin prakticne provere rezultata rada;
- literatura.

NASLOV DIPLOMSKOG (BACHELOR) RADA:  
Razvoj 2D gejm endzina sa editorom scena i optimizovanom broad-phase detekcijom kolizija zasnovanom na segmentisanoj sweep-and-prune metodi

TEKST ZADATKA:

1. Analizirati stanje u oblasti 2D gejm endzina, komponentnih arhitektura i algoritama za detekciju kolizija.
2. Izraditi specifikaciju zahteva softverskog resenja koje obuhvata rad sa scenama, komponentama, resursima, renderovanjem, audiom i korisnickim skriptama.
3. Izraditi specifikaciju dizajna fizickog podsistema sa posebnim osvrtom na broad-phase detekciju kolizija zasnovanu na sweep-and-prune pristupu i segmentisanim intervalnim listama.
4. Implementirati 2D gejm endzin sa komponentnom arhitekturom, editorom scena i javnim runtime interfejsom za pokretanje projekata.
5. Implementirati i verifikovati fizicki podsistem koji obuhvata broad-phase i narrow-phase detekciju kolizija, kao i resavanje kontakata za osnovne 2D oblike.
6. Izvrsiti prakticnu proveru resenja kroz automatizovane regresione testove, demonstracione scene i benchmark scenarije.
7. Dokumentovati teorijsku osnovu, arhitekturu, implementaciju, benchmark metodologiju, rezultate i ogranicenja razvijenog sistema.

Rukovodilac studijskog programa: ____________________  
Mentor rada: ____________________

Primerak za: studenta i mentora.

---

## Sadrzaj

1. Uvod
2. Pregled stanja u oblasti
3. Teorijski pojmovi i definicije
4. Metodologija i implementacija sistema
5. Eksperimentalna metodologija, benchmark rezultati i diskusija
6. Demonstracija implementiranog sistema
7. Zakljucak
8. Literatura
9. Biografija
10. Kljucna dokumentacijska informacija
11. Key words documentation
12. Prilog A - Predlog formata sirovih benchmark podataka

---

## 1. Uvod

Gejm endzini predstavljaju kljucnu softversku infrastrukturu za razvoj savremenih video igara i interaktivnih simulacija. Oni objedinjuju upravljanje objektima scene, sistem resursa, renderovanje, audio reprodukciju, skriptovanje, serijalizaciju sadrzaja i fizicku simulaciju u jedinstven okvir koji omogucava razvoj slozenih aplikacija na visem nivou apstrakcije. Zbog toga su gejm endzini znacajni ne samo u industriji zabave, vec i u obrazovanju, prototipizaciji interaktivnih sistema, vizuelizaciji i simulacionim okruzenjima.

U ovom radu razmatra se problem razvoja sopstvenog 2D gejm endzina koji omogucava rad sa scenama, komponentama i resursima, kao i izvrsavanje fizicke simulacije nad vecim brojem objekata. Poseban fokus rada je broad-phase detekcija kolizija, odnosno faza u kojoj je potrebno brzo izdvojiti parove objekata koji potencijalno mogu biti u sudaru. Ovaj problem je od posebne vaznosti zato sto direktno utice na ukupne performanse fizickog podsistema, narocito u scenama sa velikim brojem staticnih ili slabo pokretnih objekata i manjim brojem dinamickih tela.

Problem je resen razvojem sistema Platformator, implementiranog u programskom jeziku C++20. Arhitektura sistema zasnovana je na komponentnom modelu u kome su objekti scene prosirivi komponentama za prikaz, fiziku, audio, animaciju, kameru i korisnicka ponasanja. Za rad sa prozorom, renderovanjem i audiom koriscen je SDL3, za matematicke proracune Eigen, dok je fizicki podsistem zasnovan na grid subdiviziji prostora, AABB strukturama i segmentisanim intervalnim listama inspirisanim radom Tracy, Buss i Woods [1]. Pored jezgra endzina razvijen je i editor scena koji omogucava pregled hijerarhije objekata, inspekciju komponenti i rad sa resursima, kao i demonstracioni primer igre koji potvrdjuje prakticnu upotrebljivost sistema.

Pored funkcionalne verifikacije, posebna paznja u radu posvecena je i benchmark evaluaciji. U tu svrhu implementirani su namenski benchmark scenariji u okviru samog Platformator endzina, kao i ekvivalentni scenariji u Godot endzinu, kako bi bilo moguce izvrsiti kontrolisano poredjenje. Cilj takvog poredjenja nije da se tvrdi da je jedan endzin univerzalno brzi ili bolji od drugog, vec da se na istim ili sto slicnijim scenarijima sagledaju performanse Platformator resenja, njegovo skaliranje sa brojem objekata i odnos izmedju internih broad-phase metrika i ukupnog vremena frejma.

Evaluacija resenja sprovedena je kombinacijom automatizovanih testova, rucno vodjenih funkcionalnih scenarija, demonstracionog primera igre i benchmark merenja. Automatizovana verifikacija obuhvata regresione testove za stabilnost fizickog sistema, broad-phase logiku, serijalizaciju scena, audio i animator komponente, kao i testove bezbednog brisanja objekata i rekreacije runtime okruzenja. Benchmark metodologija je organizovana tako da posebno razdvaja headless i renderovana merenja, a rezultati se prikazuju i kroz tabele i kroz grafikone skaliranja sa brojem objekata, zauzetih celija i kolizionih parova. Konkretne numericke vrednosti benchmark rezultata nisu unapred popunjene u ovom draftu, jer ih je potrebno uneti nakon finalne normalizacije i obrade merenja.

Ostatak rada organizovan je na sledeci nacin. U drugom poglavlju dat je pregled stanja u oblasti, sa fokusom na broad-phase algoritme, gejm endzine i editore scena. Trece poglavlje uvodi teorijske pojmove potrebne za razumevanje resenja, ukljucujuci komponentnu arhitekturu, sweep-and-prune pristup, segmentisane intervalne liste, SAT test i impulsno resavanje kontakata. U cetvrtom poglavlju opisana je metodologija i implementacija sistema Platformator. Peto poglavlje sadrzi eksperimentalnu metodologiju, predlog benchmark tabela i grafikona, kao i strukturu diskusije rezultata uz poredjenje sa Godotom. U sestom poglavlju prikazana je demonstracija sistema kroz editor, benchmark scenarije i Mario primer. Na kraju rada dati su zakljucci, pravci buduceg razvoja, literatura i obavezni dokumentacioni dodaci.

---

## 2. Pregled stanja u oblasti

U ovom poglavlju predstavljena su resenja i radovi relevantni za temu razvoja gejm endzina sa akcentom na detekciju kolizija, arhitekturu sistema i alate za uredjivanje scena. Prilikom izbora srodnih radova i sistema primenjena su tri kriterijuma. Prvi kriterijum je slicnost u samom problemu broad-phase detekcije kolizija. Drugi kriterijum je slicnost u arhitekturi, odnosno upotreba komponentnog modela za organizaciju gejm objekata. Treci kriterijum je prisustvo alata za uredjivanje scena koji olaksavaju rad krajnjem korisniku i priblizavaju resenje profesionalnim alatima kakvi su Unity ili Godot.

### 2.1. Algoritmi za detekciju kolizija i broad-phase pristupi

Jedan od klasicnih radova u oblasti fizicke simulacije i sudarne detekcije je rad Baraffa [2], u kome je detaljno razmatrana simulacija krutih tela bez prodiranja. Iako taj rad ne definise direktno savremenu arhitekturu gejm endzina, on predstavlja vaznu teorijsku osnovu za kasnije broad-phase i narrow-phase pristupe. U ranim implementacijama broad-phase detekcije cesto su korisceni prostorni hijerarhijski modeli ili jednostavno pretrazivanje svih parova objekata, ali je njihov problem bila slaba skalabilnost kada broj objekata raste.

Znacajan napredak ostvaren je razvojem sweep-and-prune pristupa, u kome se AABB projekcije objekata sortiraju po osama i koriste za brzo utvrdjivanje kandidata za sudar. Dalje unapredjenje vidljivo je u sistemu I-COLLIDE [3], gde autori koriste inkrementalno odrzavanje sortiranih projekcija kako bi iskoristili vremensku koherentnost scene. Prednost ove ideje je to sto objekti izmedju susednih frejmova cesto menjaju polozaj relativno malo, pa nije potrebno iznova vrsiti skupu globalnu obradu svih projekcija. U literaturi postoje i dalje varijacije sweep-and-prune pristupa, kao sto su kineticke varijante koje unapred racunaju dogadjaje zamene susednih intervala za poznate putanje kretanja [13], ali su takvi pristupi pogodniji za specijalizovana okruzenja nego za opsti gejm endzin.

Rad Tracy, Buss i Woods [1] je posebno znacajan za ovaj diplomski rad, jer ne uvodi samo segmentisane intervalne liste, vec siri skup unapredjenja broad-phase algoritma. Autori razmatraju kombinaciju prostorne subdivizije i sweep-and-prune pristupa, batch insertion/removal obradu, event-based izlaz interfejs i segmentisane intervalne liste kao novu strukturu za efikasno umetanje i uklanjanje AABB projekcija. Za ovaj rad najrelevantniji je upravo deo koji se odnosi na subdivision i segmentisane intervalne liste, jer on direktno odgovara scenama sa velikim brojem uglavnom nepokretnih objekata i manjim brojem lokalnih promena. Upravo ta karakteristika ga cini pogodnim za 2D platformske igre i simulacione scene sa velikim brojem staticnih prepreka i manjim brojem pokretnih objekata.

Vazno je, medjutim, naglasiti da Platformator ne predstavlja potpunu reprodukciju svih varijanti iz rada [1]. U implementaciji predstavljenoj u ovom diplomskom radu preuzeti su osnovni principi subdivision plus segmented interval list pristupa i prilagodjeni 2D komponentnom gejm endzinu. Batch insertion/removal putanja opisana u originalnom radu nije implementirana u istom obliku, pa se doprinos ovog rada ogleda u adaptaciji i integraciji odabrane varijante algoritma, a ne u potpunoj reimplementaciji svih eksperimentalnih grana iz originalnog rada.

Pored broad-phase algoritama, vazan deo sistema cini i narrow-phase detekcija. U prakticnim sistemima za 2D kolizije cesto se koristi SAT, odnosno Separating Axis Theorem, koji omogucava efikasan test preseka konveksnih oblika na osnovu projekcija na konacan skup osa [4]. Prednost SAT pristupa je u tome sto, pored samog odgovora da li dva objekta kolidiraju, moze da pruzi i informaciju o normalama kontakta i dubini penetracije, sto je direktno korisno za fizicki odgovor.

### 2.2. Gejm endzini i editori scena

Savremeni gejm endzini kao sto su Unity i Godot popularizovali su pristup u kome je razvoj igre organizovan oko scene, hijerarhije objekata, komponenti i inspektora svojstava. U Unity okruzenju korisnik scene formira kombinovanjem GameObject entiteta i pridruzenih komponenti, dok se uredjivanje vrsi kroz vizuelni editor koji objedinjuje hijerarhijski pregled, inspector, asset browser i pokretanje projekta iz istog okruzenja [5]. Slican pristup nudi i Godot, koji stavlja poseban akcenat na organizaciju scene kao stabla cvorova i na integrisani editor [6].

Za akademski i inzenjerski rad vazno je uociti da komercijalni endzini resavaju veliki broj problema odjednom: renderovanje, audio, fizicku simulaciju, uvoz resursa, uredjivanje scena i izvrsavanje skripti. Medjutim, takvi sistemi su istovremeno veoma slozeni i zatvoreni za detaljno proucavanje pojedinacnih algoritamskih odluka. Zato razvoj sopstvenog, manjeg endzina predstavlja pogodan okvir za obrazovni i istrazivacki rad: omogucava studentu da razume unutrasnju strukturu sistema, implementira i testira konkretne algoritme i da zatim te odluke uporedi sa postojecim industrijskim praksama.

U kontekstu ovog rada, editor scena je vazan zato sto podize rad iznad nivoa same biblioteke. Ako krajnji korisnik moze da kreira scenu, doda objekte i komponente, poveze resurse i odmah pokrene izvrsavanje, tada sistem postaje upotrebljiv i kao razvojna platforma. Zbog toga su Unity-like karakteristike, kao sto su hijerarhija objekata, inspektor svojstava, pregled resursa i integracija sa build/run procesom, relevantne i za evaluaciju kvaliteta razvijenog resenja, a ne samo za njegovu prezentaciju.

### 2.3. Benchmarking real-time sistema

Pored same implementacije, za rad ove vrste vazna je i metodoloski ispravna evaluacija performansi. U oblasti gejm endzina i real-time sistema benchmark rezultati mogu biti varljivi ukoliko scenariji nisu dovoljno ujednaceni, ukoliko se ne razlikuju headless i renderovana merenja ili ukoliko se porede metrike koje nisu istog semantickog znacenja. Zbog toga je potrebno jasno odvojiti metrike koje su zajednicke za vise sistema od internih metrika koje sluze za analizu samo jednog sistema.

U ovom radu poredjenje sa Godotom koristi se kao referentna tacka za scenarije broad-phase, narrow-phase i rigid-body container benchmarka. Takvo poredjenje je korisno jer Godot predstavlja zreo i siroko rasprostranjen open-source endzin sa sopstvenim 2D fizickim sistemom, sto omogucava da se Platformator pozicionira u odnosu na poznato referentno resenje. Istovremeno, potrebno je otvoreno naglasiti da potpuna jednakost internih fizickih algoritama nije moguca, pa se cross-engine poredjenje mora zasnivati pre svega na scenarijski uporedivim, a ne na internim engine-specific metrikama.

### 2.4. Resenja najbliza ovom radu

Po pitanju broad-phase algoritma, resenje najblize ovom radu je upravo rad Tracy, Buss i Woods [1]. Slicnost se ogleda u tome sto i Platformator koristi grid-subdiviziju, AABB projekcije i segmentisane intervalne liste za odrzavanje kandidata za koliziju. Razlika je u tome sto je originalni rad koncipiran kao opsti pristup za velike virtuelne scene, dok je Platformator primenjen u okviru 2D gejm endzina sa komponentnom arhitekturom, sistemom scena, skriptovanjem, benchmark infrastrukturom i desktop editorom. Drugim recima, ovaj rad ne preuzima samo algoritam, vec ga ugradjuje u siri softverski proizvod.

Sa aspekta arhitekture i korisnickog iskustva, najbliza resenja su Unity i Godot [5], [6]. Iako Platformator ne tezi da po obimu i stepenu zrelosti konkurise tim sistemima, od njih preuzima organizacione ideje koje su se pokazale uspesnim u praksi: scene kao osnovnu jedinicu sadrzaja, komponentno modelovanje ponasanja i vizuelni editor za rad sa objektima i resursima. U tom smislu, doprinos rada nije u tome da zameni industrijski alat, vec da prikaze kako se kombinacijom istrazivackog algoritma i inzenjerske arhitekture moze formirati koherentno razvojno okruzenje za 2D igre.

Na osnovu pregleda stanja u oblasti moze se zakljuciti da postoje tri glavna pravca na koja se ovaj rad oslanja. Prvi je algoritamski pravac, olicen u radovima o broad-phase detekciji kolizija i SAT obradi kontakata. Drugi je inzenjersko-alatni pravac, olicen u savremenim gejm endzinima sa integrisanim editorima. Treci je metodoloski pravac, odnosno potreba da se performanse ovakvog sistema prikazu i diskutuju kroz benchmark scenarije koji su dovoljno kontrolisani da omoguce smisleno poredjenje. Upravo kombinacija ta tri pravca predstavlja osnovnu motivaciju i glavnu posebnost sistema Platformator.

---

## 3. Teorijski pojmovi i definicije

U ovom poglavlju predstavljeni su teorijski koncepti neophodni za razumevanje implementacije sistema Platformator. Najpre je razmotrena komponentna arhitektura gejm endzina, zatim broad-phase detekcija kolizija zasnovana na sweep-and-prune pristupu i segmentisanim intervalnim listama, a na kraju i narrow-phase obrada kontakata uz primenu SAT pristupa i impulsnog resavanja kolizija.

### 3.1. Komponentna arhitektura gejm endzina

Komponentna arhitektura polazi od ideje da objekat scene ne treba modelovati dubokim nasledjivanjem klasa, vec kompozicijom manjih, jasno definisanih funkcionalnih delova. U takvom modelu GameObject predstavlja identitet i transformaciju objekta, dok se konkretne sposobnosti dodaju kroz komponente, kao sto su Sprite, Collider, Rigidbody, Audio, Camera, Animator ili ScriptComponent. Prednost ovakvog pristupa je u visokoj fleksibilnosti: isti objekat moze u jednoj sceni biti samo vizuelni entitet, a u drugoj i fizicko telo, audio izvor i nosilac skripti.

U sistemima inspirisanim Unity pristupom komponenta je obicno povezana sa jednim objektom scene, dok editor omogucava dodavanje i uredjivanje komponenti bez rucnog menjanja koda za osnovnu strukturu objekta. Time se postize razdvajanje odgovornosti izmedju sistema koji upravljaju renderovanjem, fizikom, audio reprodukcijom i ponasanjem. U okviru ovog rada takav model je vazan ne samo zbog preglednosti implementacije, vec i zato sto olaksava vezivanje fizickog podsistema sa sistemom scena, benchmark infrastrukturom i editorom.

### 3.2. AABB i broad-phase detekcija kolizija

Axis-Aligned Bounding Box, odnosno AABB, predstavlja jedan od najcesce koriscenih oblika aproksimacije geometrije objekta u broad-phase fazi detekcije kolizija. AABB je definisan minimalnim i maksimalnim koordinatama po osama, pri cemu su stranice pravougaonika ili pravougaonog bloka poravnate sa koordinatnim osama. Prednost ovog pristupa je sto se presek dva AABB-a moze proveriti veoma efikasno poredeci njihove intervale po osama.

Broad-phase detekcija kolizija koristi upravo ovakve jednostavne omotace kako bi veliki broj parova objekata bio eliminisan pre nego sto se nad manjim skupom kandidata primene skuplji geometrijski testovi. U kompleksnim scenama performanse cele fizicke simulacije cesto najvise zavise upravo od kvaliteta broad-phase podsistema, jer on odredjuje koliko parova se prosledjuje u narrow-phase obradu.

### 3.3. Sweep-and-prune pristup

Sweep-and-prune pristup broad-phase detekciji kolizija zasniva se na projekciji AABB granica objekata na jednu ili vise koordinatnih osa. Ako se projekcije dva objekta ne preklapaju ni na jednoj osi, tada je sigurno da se objekti ne seku. Ako se preklapaju na svim posmatranim osama, par se prosledjuje u narrow-phase obradu. Kljucna prednost ove metode je to sto se veliki broj potencijalnih parova moze odbaciti veoma rano, pre nego sto se primene skuplji geometrijski testovi.

U klasicnoj verziji algoritma odrzava se sortirana lista pocetaka i krajeva intervala. Kada se minimum nekog intervala pojavi pre maksimuma drugog intervala, izmedju odgovarajucih objekata postoji preklapanje na toj osi. Problem tradicionalnog pristupa nastaje kada je potrebno efikasno obraditi dodavanje, uklanjanje ili vece pomeranje objekata bez ponovnog globalnog sortiranja velikog niza projekcija.

### 3.4. Segmentisane intervalne liste

Segmentisane intervalne liste resavaju prethodno opisani problem podelom velike liste na manje lokalno sortirane celine, odnosno segmente ili chunk-ove [1]. U originalnom radu struktura se moze posmatrati kao hibrid izmedju povezanog niza manjih sortiranih blokova i hijerarhijskog pristupa pretrazi. Svaki segment cuva mali sortirani niz projekcija, a pomocni checkpoint skup odrzava informaciju o intervalima koji prelaze granice segmenta. Ideja je konceptualno bliska nekom obliku unrolled list strukture [9], ali prosirena dodatnim informacijama potrebnim za broad-phase pretragu i odrzavanje kandidatskih parova.

Pri dodavanju intervala nova projekcija se najpre pozicionira u odgovarajuci segment, zatim lokalno sortira unutar segmenta, dok checkpoint skupovi obezbedjuju da nije potrebno skenirati celu osu da bi se pronasli potencijalno relevantni veliki intervali. Kada se segment prepuni, on se deli na dva manja segmenta, a pri smanjenju popunjenosti susedni segmenti se mogu spojiti. Originalni rad takodje razmatra poseban slucaj brzog kretanja u kome minimum i maksimum istog intervala mogu privremeno zameniti redosled preko granice segmenta, pa se za ispravno odrzavanje checkpoint informacija uvodi dodatni pomocni skup za kompenzaciju takvih prelaza [1].

U ovom radu taj teorijski model je dodatno povezan sa prostornom subdivizijom scene u grid celije. Time se smanjuje gustina projekcija po pojedinacnoj strukturi, jer svaka celija odrzava sopstvene lokalne AABB parove. Takva kombinacija spaja ideju spatial subdivision pristupa i ideju inkrementalnog odrzavanja intervalnih listi. Takodje je vazno naglasiti da originalni rad [1], pored segmentisanih intervalnih listi, opisuje i batch insertion/removal i event-based output pristupe, ali ti delovi nisu implementirani kao zasebne paralelne grane u Platformator sistemu. Oni se u ovom radu koriste pre svega kao teorijski i komparativni kontekst.

### 3.5. SAT i resavanje kontakata

Nakon broad-phase filtriranja, kandidat-parovi se proveravaju u narrow-phase fazi. Za 2D konveksne oblike, poput pravougaonika i krugova, praktican izbor predstavlja SAT. Teorema o razdvajajucoj osi kaze da se dva konveksna oblika ne seku ako postoji bar jedna osa na kojoj se njihove projekcije ne preklapaju [4]. U slucaju da takva osa ne postoji, oblici se seku. Pored same detekcije, minimum preklapanja po svim posmatranim osama daje korisnu aproksimaciju normale i dubine penetracije.

Fizicki odgovor na koliziju u ovom radu zasniva se na impulsnom resavanju kontakata. Umesto resavanja kompletne dinamike u jednom velikom sistemu jednacina, kontaktne tacke se iterativno koriguju kroz normalne i tangentne impulse. Ovakav pristup je rasprostranjen u real-time fizickim sistemima zato sto pruza dobar kompromis izmedju stabilnosti, performansi i slozenosti implementacije. U okviru prakticnog gejm endzina takav izbor omogucava dovoljno uverljivo ponasanje rigidbody objekata, a da sistem ostane dovoljno brz za izvrsavanje u svakom frejmu.

### 3.6. Benchmark metrike i skaliranje

Kod benchmark evaluacije real-time fizickih sistema potrebno je razlikovati najmanje tri nivoa metrika. Prvi nivo cine krajnje korisnicke metrike, kao sto su ukupno vreme frejma i eventualno vreme fizickog koraka. Drugi nivo cine engine-specific metrike, na primer vreme broad-phase ili narrow-phase obrade. Treci nivo cine strukturne metrike koje opisuju stanje scene tokom merenja, kao sto su broj objekata, broj zauzetih grid celija i broj kolizionih ili kandidatskih parova.

Skaliranje sistema sa brojem objekata, zauzetih celija ili kolizionih parova predstavlja poseban fokus ovog rada, jer omogucava da se analizira ne samo apsolutna brzina izvrsavanja, vec i odnos izmedju strukture scene i rasta troska obrade. Upravo zbog toga benchmark poglavlje ne treba da prikaze samo jednu tabelu sa prosecnim vremenom, vec i grafikone koji ilustruju trendove i omogucavaju diskusiju o tome kada sistem ostaje linearan, kada prelazi u strmiji rast i kako se ti trendovi razlikuju izmedju Platformator i Godot resenja.

---

## 4. Metodologija i implementacija sistema

U ovom poglavlju predstavljena je implementacija sistema Platformator. Ulaz u sistem cine definicije scena, resursi projekta i korisnicke akcije u editoru ili samoj igri. Ocekivani izlaz sistema su izvrsive 2D scene sa renderovanim objektima, odigranim audio sadrzajem, obradjenim skriptama i stabilnom fizickom simulacijom kolizija. Sistem je podeljen na vise medjusobno povezanih modula: jezgro runtime okruzenja, fizicki podsistem, editor scena, demonstracioni primer igre i benchmark infrastruktura.

Na visokom nivou apstrakcije arhitektura moze se prikazati sledecim tokom podataka:

```text
Editor scena / Mario primer / Benchmark runner
        |
        v
   scena + assets + skripte
        |
        v
  Runtime / GameManager
        |
        +--> Renderer + Audio
        |
        +--> Script sistem
        |
        +--> PhysicsManager
                |
                +--> Grid
                +--> AABB po celiji
                +--> SegmentedIntervalList
                +--> SAT + kontaktne tacke
                +--> impulsno resavanje
        |
        +--> BenchmarkRecorder
```

### 4.1. Jezgro sistema i komponentni model

Centralna izvrsna celina sistema je runtime, koji instancira GameManager i preko njega upravlja scenom, objektima, prozorom i pomocnim podsistemima. GameObject predstavlja osnovni entitet scene i sadrzi transformaciju, identitet, roditeljsko-dete relacije i skup pridruzenih komponenti. Na ovaj nacin je omoguceno da se ponasanje objekata gradi kompozicijom, a ne kreiranjem velikog broja specijalizovanih klasa.

Implementirane komponente obuhvataju vizuelne, fizicke i logicke aspekte scene. Sprite komponenta zaduzena je za prikaz tekstura. Collider komponente modeluju geometriju objekta relevantnu za kolizije, dok Rigidbody uvodi masu, brzinu, sile i tip tela. Animator upravlja smenom frejmova animacije, Audio omogucava reprodukciju zvuka, Camera definise pogled na scenu, a ScriptComponent je namenjen vezivanju korisnickih ponasanja uz objekte scene.

Ovako organizovan sistem je prilagodjen i radu kroz editor i radu kao biblioteka. Korisnik moze da pokrene stock runner i zada putanju do scene, ali moze i da koristi javni `platformator::Runtime` interfejs direktno iz sopstvenog `main` programa. Time se Platformator ne postavlja samo kao interni eksperiment, vec i kao razvojna osnova za druge projekte.

### 4.2. Scene, serijalizacija i resursi

Scene su u Platformator sistemu cuvane u tekstualnom formatu pogodnom za serijalizaciju i naknadnu obradu. Objekti scene sadrze identifikatore, transformacije, hijerarhijske veze i kolekcije komponenti. Na taj nacin je omoguceno da editor i runtime rade nad istom logickom reprezentacijom scene.

Rad sa resursima organizovan je tako da teksture, audio fajlovi, scene i drugi asset-i mogu da se referenciraju iz vise delova sistema bez bespotrebnog dupliranja. Ovakva organizacija je vazna i za editor i za benchmark infrastrukturu, jer omogucava da se scenariji reproducibilno pokrecu iz istih ulaznih podataka.

### 4.3. Fizicki podsistem i detekcija kolizija

Fizicki podsistem organizovan je oko PhysicsManager komponente koja upravlja rigidbody objektima, kolajderima, sinhronizacijama i aktivnim sudarima. Pri promeni polozaja ili geometrije kolajdera vrsi se osvezavanje AABB projekcija, kao i sinhronizacija grid celija koje objekat pokriva. Time se broad-phase logika odrzava inkrementalno, bez potrebe za potpunom rekonstrukcijom svih struktura pri svakom frejmu. Na nivou opste ideje ovaj podsistem najblize prati subdivision plus segmented interval list pravac iz rada Tracy, Buss i Woods [1], ali je prilagodjen 2D domenu i integrisan sa ostatkom gejm endzina.

Prostor scene deli se na grid celije. Svaka celija poseduje sopstvenu AABB strukturu sa X i Y intervalnim listama. Kandidat-parovi koji se preklapaju u svim osama unutar iste celije predstavljaju lokalne svedoke preklapanja. Na nivou celog sistema koristi se witness counting mehanizam kojim se evidentira da li se par objekata preklapa u najmanje jednoj celiji. Na taj nacin sistem korektno obradjuje objekte koji obuhvataju vise celija istovremeno.

Za odrzavanje lokalnog redosleda projekcija koriste se segmentisane intervalne liste. Svaki segment cuva mali sortirani niz projekcija i checkpoint skup. Pri dodavanju ili uklanjanju intervala sistem najpre locira odgovarajuci segment, zatim lokalno azurira njegov sadrzaj, a u slucajevima prelazenja granice segmenta azurira checkpoint informaciju i eventualno deli ili spaja segmente. Ovakva organizacija je pogodna za 2D platformske scene u kojima postoji veliki broj staticnih platformi, zidova i dekorativnih objekata, dok se relativno mali broj objekata aktivno krece kroz prostor. Batch insertion/removal postupak opisan u radu [1] nije implementiran kao posebna alternativna putanja, tako da se Platformator broad-phase oslanja na inkrementalno odrzavanje i lokalne popravke strukture, a ne na batch integraciju svih insertion/removal dogadjaja u jednoj prolaznoj fazi.

Nakon broad-phase faze, kandidat-parovi prolaze kroz SAT proveru. Za pravougaonike se koriste ose definisane njihovim normalama, dok se za krugove i mesovite parove koriste odgovarajuce ose i pomocni geometrijski proracuni. Iz SAT faze dobijaju se normalni pravac kontakta, penetracija i tacke kontakta, nakon cega impulsni solver iterativno koriguje brzine i ugaone brzine objekata. Dodatno su implementirane podrske za trenje, restituciju, kinematicka tela i mehanizam uspavljivanja rigidbody objekata koji stabilno pocivaju na podlozi.

### 4.4. Editor scena nalik na Unity interfejs

Pored jezgra endzina, razvijen je i desktop editor scena koji funkcionise kao zaseban alat. Editor je implementiran u Python okruzenju uz PySide6 i pruza vise panela koji prate logiku savremenih razvojnih okruzenja za igre. Korisniku su na raspolaganju panel hijerarhije objekata, inspektor svojstava, pregled biblioteke ponasanja, pregled resursa i izlazni panel sa porukama build i run procesa.

Scene se u editoru predstavljaju modelima koji odrazavaju isti format koji koristi runtime. To omogucava round-trip scenario u kome korisnik otvori postojecu scenu, izvrsi izmene i ponovo je sacuva bez gubitka poznatih podataka. Posebna paznja posvecena je normalizaciji putanja ka resursima tako da se one cuvaju u stabilnom obliku zasnovanom na projektnim `assets/...` pravilima.

Unity-like karakter ovog editora ogleda se pre svega u organizaciji rada. Objekti su prikazani hijerarhijski, komponente se uredjuju kroz inspector, resursi su izdvojeni u zaseban prikaz, a build i pokretanje scene mogu se obaviti iz samog alata. Na taj nacin se dobija okruzenje pogodno ne samo za demonstraciju endzina, vec i za praktican razvoj manjih projekata.

### 4.5. Mario primer kao demonstracija potrosaca biblioteke

Da bi se pokazalo da Platformator nije vezan samo za interni testni kod, razvijen je Mario primer kao zaseban potrosac biblioteke. Primer koristi javni runtime interfejs endzina, sopstvene C++ skripte za ponasanje igraca, kamera rig, patrolirajuce neprijatelje, novcice i ciljnu zastavu, kao i posebnu scenu i resurse koji se kopiraju u runtime direktorijum pri izgradnji.

Mario primer ima dvostruku ulogu u radu. Sa jedne strane, on predstavlja demonstraciju da je endzin dovoljno kompletan da podrzi jednostavnu platformsku igru sa skriptama, kolizijama, animacijama i audio sadrzajem. Sa druge strane, on sluzi kao scenario za proveru integracije izmedju scene, runtime-a, komponentnog sistema i fizickog podsistema. Upravo zbog toga ovaj primer treba posmatrati kao deo validacije upotrebljivosti, a ne samo kao vizuelni dodatak projektu.

### 4.6. Benchmark infrastruktura

Za potrebe kvantitativne evaluacije implementiran je namenski benchmark runner za Platformator, kao i prateci Godot benchmark projekat sa ekvivalentnim scenarijima. U Platformator delu benchmark infrastruktura je realizovana kroz poseban izvrsni program koji moze da gradi i pokrece tri scenarija: `broad_phase`, `narrow_phase` i `rigid_body_container`. Runner podrzava headless i renderovan rezim rada, kao i podesavanje broja warmup frejmova, broja merenih frejmova, fiksnog vremenskog koraka i broja kutija i krugova u rigid-body container scenariju.

Platformator benchmark recorder prikuplja i stampa skup scope i counter metrika. Scope metrike obuhvataju ukupno vreme frejma, vreme broad-phase obrade, vreme narrow-phase obrade i vreme resavanja kolizija. Counter metrike obuhvataju broj objekata, broj zauzetih celija, broj kandidat parova, broj pending narrow-phase parova, broj aktivnih kolizija, broj budnih dinamickih tela i vise pomocnih brojackih vrednosti za ulazne i izlazne kolizione dogadjaje.

Godot benchmark projekat implementira paralelne scenarije `broad_phase`, `narrow_phase` i `rigid_body_container`, pri cemu se merenje oslanja na engine Performance monitore. Godot stampa prosecno, medijansko i p95 vreme frejma i `physics_process` faze, kao i prosecne vrednosti broja objekata, broja cvorova i 2D collision pair monitora. Ovakva organizacija omogucava da se za cross-engine poredjenje koriste zajednicke metrike, dok se Platformator-specific metrike koriste za dublju internu analizu Platformator resenja.

### 4.7. Korisceni alati

Jezgro endzina razvijeno je u programskom jeziku C++20. Za rad sa prozorom, dogadjajima, renderovanjem i audio sadrzajem koriscen je SDL3 i pratece biblioteke SDL3_image, SDL3_ttf i SDL3_mixer [7]. Matematicki proracuni zasnivaju se na biblioteci Eigen. Za odabrane paralelne delove obrade koristi se oneTBB. Format scena zasnovan je na tekstualnoj serijalizaciji, a sistem za izgradnju projekta zasniva se na CMake-u.

Editor scena razvijen je u Python-u uz PySide6. Benchmark poredjenje koristi i Godot 4 projekat sa odgovarajucim benchmark scenama. Ovakav izbor je opravdan time sto desktop alat zahteva brzu iteraciju nad korisnickim interfejsom, dok C++ jezgro ostaje zaduzeno za performantni runtime. Kombinacija C++ jezgra, Python editora i Godot referentnih benchmarka predstavlja praktican kompromis izmedju performansi, brzine razvoja alata i uporedive evaluacije.

---

## 5. Eksperimentalna metodologija, benchmark rezultati i diskusija

U ovom poglavlju prikazan je nacin na koji je verifikovano da sistem Platformator ispunjava osnovne funkcionalne i arhitekturalne zahteve, ali i kako je organizovana benchmark evaluacija sistema. Za razliku od radova iz oblasti masinskog ucenja, ovde nije rec o klasicnom skupu podataka za treniranje i testiranje modela, vec o skupu test scenarija, automatizovanih regresionih provera i sinteticnih benchmark scena. Zbog toga je evaluacija organizovana tako da obuhvati korektnost implementacije, stabilnost rada, prakticnu upotrebljivost i performanse.

### 5.1. Ciljevi evaluacije i istrazivacka pitanja

Benchmark evaluacija u ovom radu postavljena je tako da odgovori na sledeca pitanja:

1. Kako se ukupno vreme frejma menja sa porastom broja objekata u kontrolisanim benchmark scenarijima?
2. Kako se broad-phase vreme Platformator sistema menja sa porastom broja zauzetih grid celija?
3. Kako broj kolizionih ili kandidatskih parova utice na ukupno vreme fizicke obrade?
4. Kako se Platformator ponasa u poredjenju sa Godot referentnim scenarijima kada se koriste sto slicnije postavke i zajednicke metrike?
5. Koja je razlika izmedju headless i renderovanih merenja i koliko prikaz scene menja zakljucke o performansama fizickog podsistema?

Ovakva formulacija evaluacije vazna je zato sto pomera fokus sa jedne brojke, na primer prosecnog vremena frejma, ka razumevanju toga kako se sistem ponasa pri promeni strukture scene. Time benchmark poglavlje dobija istrazivacku, a ne samo demonstracionu vrednost.

Pri tome je vazno naglasiti da benchmark evaluacija u ovom radu nije pokusaj doslovne reprodukcije eksperimentalne postavke iz rada [1]. Originalni rad meri 3D broad-phase varijante, ukljucujuci batch insertion/removal i poredjenje sa octree pristupom, u sintetickim uniformnim okruzenjima i sa jednonitnim izvrsavanjem na tacno odredjenom hardveru. Platformator, nasuprot tome, koristi 2D engine kontekst, integrisanu fiziku i benchmark runner koji meri i engine-level scope i counter metrike. Zbog toga rezultate iz ovog rada treba tumaciti kao evaluaciju implementirane 2D adaptacije i njene upotrebljivosti u okviru gejm endzina, a ne kao tvrdnju da je originalni eksperiment iz [1] reprodukovan u celini.

### 5.2. Funkcionalna verifikacija sistema

Automatizovana verifikacija sprovedena je kroz vise izvrsnih test programa. Najvazniji medju njima je `platformator_regression_tests`, koji sadrzi regresione scenarije za fiziku, broad-phase, serijalizaciju, audio, animator i rad sa hijerarhijom objekata. Dodatno postoje posebni testovi za round-trip skriptovanih scena, bezbedno brisanje objekata koji su u redu cekanja za obradu, kao i za ponovno kreiranje runtime okruzenja u istom procesu.

Tabela 5.1 prikazuje rezultat automatizovane funkcionalne verifikacije nad projektno definisanim test izvrsnim programima.

| Test izvrsni program | Svrha | Rezultat |
| --- | --- | --- |
| `platformator_regression_tests` | Regresiona provera fizike, broad-phase logike, serijalizacije, animacije, audia i rada sa objektima | Prosao |
| `platformator_scene_script_roundtrip_test` | Provera serijalizacije scena i skripti | Prosao |
| `platformator_queued_deletion_safety_test` | Provera bezbednog brisanja objekata tokom obrade | Prosao |
| `platformator_runtime_recreation_test` | Provera sekvencijalnog kreiranja i gasenja runtime okruzenja | Prosao |

Pored zbirnog rezultata iz Tabele 5.1, bitno je istaci da regresioni test program pokriva vise scenarija koji obuhvataju stabilnost kolizionog sistema, checkpoint logiku segmentisanih intervalnih listi, spajanje chunk-ova, round-trip animacionih i audio podataka, kao i rad sa parent-child hijerarhijom objekata. Ovakva sirina pokrivenosti je vazna zato sto pokazuje da sistem nije validiran samo kroz jedan demonstracioni primer, vec i kroz skup ciljanih tehnickih provera.

### 5.3. Benchmark scenariji

Za benchmark evaluaciju koriscena su tri osnovna scenarija koja postoje i u Platformator i u Godot varijanti.

#### 5.3.1. Broad-phase scenario

Scenario `broad_phase` fokusiran je na velik broj horizontalno rasporedjenih oblasti ili tela koja se krecu po unapred definisanim trakama. Osnovna svrha ovog scenarija je da optereti broad-phase fazu tako sto generise veliki broj objekata koji se krecu kroz prostor, ali uz relativno jednostavnu geometriju i kontrolisan obrazac kretanja. Ovaj scenario je posebno pogodan za analizu skaliranja sa brojem objekata i za pracenje veze izmedju broja objekata, broja zauzetih celija i broad-phase vremena obrade.

#### 5.3.2. Narrow-phase scenario

Scenario `narrow_phase` konstruisan je tako da generise veci broj preklapanja i okidanja kontakata medju pravougaonim oblastima ili telima. Za razliku od `broad_phase` scenarija, ovde je veci akcenat na kolicini potencijalnih i stvarnih kolizionih parova, pa je scenario koristan za analizu toga kako rast broja kolizionih parova utice na vreme narrow-phase obrade i na ukupno vreme fizike.

#### 5.3.3. Rigid-body container scenario

Scenario `rigid_body_container` sadrzi veci broj dinamickih krugova i kutija zatvorenih unutar velikog pravougaonog kontejnera. Ovaj scenario je najblizi realnoj 2D fizickoj simulaciji sa velikim brojem rigidbody objekata, jer istovremeno opterecuje broad-phase, narrow-phase i solver kontakata. U radu je posebno koristan za poredjenje Platformator i Godot resenja u headless i renderovanom rezimu, kao i za analizu skaliranja sa ukupnim brojem objekata i brojem kontakata.

Tabela 5.2 daje pregled benchmark scenarija i njihove osnovne namene.

| Scenario | Osnovni fokus | Zajednicki za Platformator i Godot | Pogodan za cross-engine poredjenje |
| --- | --- | --- | --- |
| `broad_phase` | rast troska sa brojem pokretnih objekata | Da | Da |
| `narrow_phase` | rast troska sa brojem preklapanja | Da | Da |
| `rigid_body_container` | opterecenje cele fizike i renderovanja | Da | Da |

### 5.4. Metrike koje se mere

Da bi benchmark evaluacija bila metodoloski korektna, metrike su podeljene na zajednicke i engine-specific metrike.

#### 5.4.1. Zajednicke metrike za Platformator i Godot

Za direktno poredjenje Platformator i Godot scenarija koriste se samo metrike koje imaju dovoljno slicno znacenje u oba sistema:

- prosecno vreme frejma [ms]
- medijansko vreme frejma [ms]
- p95 vreme frejma [ms]
- prosecno vreme fizickog koraka [ms]
- medijansko vreme fizickog koraka [ms]
- p95 vreme fizickog koraka [ms]
- prosecni broj objekata u sceni
- prosecni broj kolizionih parova ili engine-level collision pair monitora

Kod Godot sistema vreme fizickog koraka odgovara `physics_process` metrici. Kod Platformator sistema kao najbliza zajednicka aproksimacija koristi se ukupno vreme frejma, dok se fizicki trosak dodatno prikazuje i kroz interne scope metrike. U finalnoj verziji rada potrebno je eksplicitno navesti kako je odabrana najbliza zajednicka definicija `physics time` kolone, odnosno da li se koristi zbir `broad_phase + narrow_phase + resolve_collisions`, ili zasebno navodjenje tih komponenti uz napomenu da Godot ne izdvaja istu dekompoziciju.

#### 5.4.2. Platformator-specific metrike

Platformator benchmark recorder dodatno pruza interne scope i counter metrike koje nisu dostupne u Godotu i zato sluze samo za internu analizu Platformator sistema:

Scope metrike:

- `frame`
- `broad_phase`
- `narrow_phase`
- `resolve_collisions`

Counter metrike:

- `object_count`
- `occupied_cell_count`
- `candidate_pair_count`
- `pending_narrow_phase_pair_count`
- `active_collision_count`
- `awake_dynamic_body_count`
- `queued_add_count`
- `queued_sync_count`
- `queued_remove_count`
- `collision_enter_event_count`
- `collision_stay_event_count`
- `collision_exit_event_count`

Ove metrike su posebno vazne za grafikone skaliranja sa brojem celija i brojem kolizionih parova. Na primer, `occupied_cell_count` je relevantna metrika za analizu uticaja granularnosti grid subdivizije na broad-phase vreme, dok `candidate_pair_count` i `active_collision_count` omogucavaju da se proveri da li rast vremena prati rast kolizionog opterecenja.

#### 5.4.3. Godot-specific metrike

Godot benchmark harness stampa sledece metrike:

- `frame`
- `physics_process`
- `object_count`
- `node_count`
- `physics_2d_collision_pairs`

Od ovih metrika `node_count` je korisna za internu interpretaciju Godot rezultata, ali nije direktno uporediva sa Platformator internim brojem celija ili kandidatskih parova. Zbog toga se u finalnim tabelama `node_count` moze navesti kao pomocna informacija, ali ne treba da bude osnova glavnih cross-engine zakljucaka.

### 5.5. Eksperimentalna postavka i normalizacija benchmarka

Da bi poredjenje Platformator i Godot benchmarka bilo sto postenije, potrebno je usvojiti jedinstvenu metodologiju merenja. U finalnoj verziji rada preporucuje se sledeci postupak:

1. Koristiti isti hardver i isti operativni sistem za oba sistema.
2. Za oba sistema odvojeno meriti headless i renderovane scenarije.
3. Koristiti release ili production buildove za finalna merenja.
4. Koristiti isti broj warmup frejmova i isti broj merenih frejmova.
5. Koristiti isti fiksni vremenski korak ili isti physics FPS gde god je to moguce.
6. Za rigid-body container scenario koristiti iste vrednosti `box_count` i `circle_count` u oba sistema.
7. Svaki eksperiment ponoviti najmanje `[dopuniti, npr. 5]` puta i koristiti proseke ili medijane po pokretanju.
8. Odvojeno prikazati cross-engine zajednicke metrike i Platformator-only interne metrike.

Tabela 5.3 predstavlja sablon za opis hardverskog i softverskog okruzenja.

| Parametar | Vrednost |
| --- | --- |
| Procesor | [dopuniti] |
| Broj jezgara / niti | [dopuniti] |
| RAM | [dopuniti] |
| GPU | [dopuniti] |
| Operativni sistem | [dopuniti] |
| Kompajler za Platformator | [dopuniti] |
| Build preset za Platformator | `benchmark-release` ili `[dopuniti]` |
| Godot verzija | [dopuniti] |
| Godot build | release export / `[dopuniti]` |
| Rezolucija renderovanih testova | [dopuniti] |
| Warmup frejmovi | [dopuniti] |
| Mereni frejmovi | [dopuniti] |
| Broj ponavljanja svakog eksperimenta | [dopuniti] |

Preporuceni primer komandi za pokretanje benchmarka prikazan je u nastavku. Konkretne vrednosti parametara treba uneti nakon usvajanja finalne benchmark matrice.

Platformator headless primer:

```bash
cmake --build --preset benchmark-release --target platformator_benchmark_runner
./bin/benchmark-release/platformator_benchmark_runner \
  --scenario broad_phase \
  --warmup-frames <warmup> \
  --measure-frames <measure> \
  --dt <seconds>
```

Platformator renderovani primer:

```bash
./bin/benchmark-release/platformator_benchmark_runner \
  --scenario rigid_body_container \
  --warmup-frames <warmup> \
  --measure-frames <measure> \
  --box-count <count> \
  --circle-count <count> \
  --render
```

Godot headless primer:

```bash
godot --headless --path benchmark/godot res://rigid_body_container.tscn -- \
  --warmup-frames <warmup> \
  --measure-frames <measure> \
  --physics-fps <fps> \
  --box-count <count> \
  --circle-count <count>
```

Godot renderovani release primer:

```bash
./benchmark/godot/RigidBodyContainerRelease.x86_64 -- \
  --warmup-frames <warmup> \
  --measure-frames <measure> \
  --physics-fps <fps> \
  --box-count <count> \
  --circle-count <count>
```

#### 5.5.1. Sta sme, a sta ne sme da se poredi direktno

Za korektnu diskusiju rezultata potrebno je eksplicitno naglasiti sledece:

- `occupied_cell_count` nema direktan Godot ekvivalent i koristi se samo za internu Platformator analizu.
- `candidate_pair_count` i `active_collision_count` nisu potpuno isti kao Godot `physics_2d_collision_pairs`, pa se pri cross-engine poredjenju moraju tretirati kao priblizni indikatori gustine kontakata, a ne kao identicna velicina.
- Platformator scope metrika `broad_phase` nema direktan Godot scope parnjak i zato se ne upisuje u glavnu cross-engine tabelu, vec u zasebnu internu Platformator tabelu.

Ova ogranicenja ne treba skrivati, vec upravo isticati, jer time benchmark poglavlje dobija metodolosku korektnost. Dodatno, posto batch insertion/removal putanja iz rada [1] nije implementirana u Platformator sistemu, u ovom radu ne treba praviti tabele ili grafikone koji bi sugerisali interno poredjenje segmentisanih intervalnih listi sa batch varijantom ukoliko takvo merenje zaista nije sprovedeno.

### 5.6. Rezultati - sabloni tabela za popunjavanje

U nastavku su dati sabloni tabela za rezultate. Namerno nisu popunjeni numerickim vrednostima.

#### 5.6.1. Cross-engine rezultati za headless broad-phase scenario

Tabela 5.4. Poredjenje Platformator i Godot rezultata u headless `broad_phase` scenariju.

| Endzin | Broj objekata | Warmup | Measure | Avg frame [ms] | Median frame [ms] | p95 frame [ms] | Avg physics [ms] | Median physics [ms] | p95 physics [ms] | Avg collision pairs | Napomena |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Platformator | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| Godot | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |

#### 5.6.2. Cross-engine rezultati za headless narrow-phase scenario

Tabela 5.5. Poredjenje Platformator i Godot rezultata u headless `narrow_phase` scenariju.

| Endzin | Broj objekata | Avg frame [ms] | Median frame [ms] | p95 frame [ms] | Avg physics [ms] | Median physics [ms] | p95 physics [ms] | Avg collision pairs | Napomena |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Platformator | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| Godot | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |

#### 5.6.3. Cross-engine rezultati za rigid-body container scenario

Tabela 5.6. Poredjenje Platformator i Godot rezultata u headless `rigid_body_container` scenariju.

| Endzin | Box count | Circle count | Avg frame [ms] | Median frame [ms] | p95 frame [ms] | Avg physics [ms] | Median physics [ms] | p95 physics [ms] | Avg object count | Avg collision pairs |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Platformator | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| Godot | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |

Tabela 5.7. Poredjenje Platformator i Godot rezultata u renderovanom `rigid_body_container` scenariju.

| Endzin | Rezolucija | Box count | Circle count | Avg frame [ms] | Median frame [ms] | p95 frame [ms] | Avg physics [ms] | Avg collision pairs | Napomena |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Platformator | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| Godot | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |

#### 5.6.4. Platformator interne metrike

Tabela 5.8. Interna dekompozicija Platformator benchmark rezultata za `rigid_body_container` scenario.

| Broj objekata | Avg frame [ms] | Avg broad_phase [ms] | Avg narrow_phase [ms] | Avg resolve_collisions [ms] | Avg occupied_cell_count | Avg candidate_pair_count | Avg pending_narrow_phase_pair_count | Avg active_collision_count |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |

Tabela 5.9. Analiza uticaja broja zauzetih celija na broad-phase vreme u Platformator sistemu.

| Scenario | Parametar scene | Avg object_count | Avg occupied_cell_count | Avg broad_phase [ms] | Avg candidate_pair_count | Komentar |
| --- | --- | --- | --- | --- | --- | --- |
| [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |
| [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] | [dopuniti] |

#### 5.6.5. Predlog kratke tekstualne interpretacije uz tabele

Nakon svake od prethodnih tabela preporucuje se kratak interpretativni pasus u sledecem formatu:

`U Tabeli 5.x prikazani su rezultati scenarija [naziv scenarija]. Uocava se da [glavni trend], dok [drugi trend]. Platformator ostvaruje [bolji/slabiji/uporediv] rezultat u odnosu na Godot u pogledu [metrika], dok se razlika smanjuje ili povecava kada [uslov]. Pretpostavljamo da je ovakav odnos posledica [razumno objasnjenje], mada treba imati u vidu da [ogranicenje merenja].`

### 5.7. Grafikoni skaliranja - predlog strukture

U ovom radu nije dovoljno prikazati samo tabele sa merenjima, vec i grafikone koji ilustruju trendove. U nastavku je predlozen skup grafikona koji direktno odgovara zahtevima rada.

#### 5.7.1. Grafik skaliranja sa brojem objekata

Grafik 5.1. Skaliranje vremena frejma sa ukupnim brojem objekata u `rigid_body_container` scenariju.

- X osa: ukupan broj objekata (`box_count + circle_count`)
- Y osa: prosecan `frame` ili `physics` time [ms]
- Serije: Platformator headless, Godot headless, Platformator rendered, Godot rendered

Predlog mesta za figuru:

```text
Slika/Grafik 5.1 Ovde umetnuti grafik skaliranja sa brojem objekata.
```

#### 5.7.2. Grafik skaliranja sa brojem celija

Grafik 5.2. Zavisnost broad-phase vremena od prosecnog broja zauzetih grid celija u Platformator sistemu.

- X osa: `avg occupied_cell_count`
- Y osa: `avg broad_phase [ms]`
- Serije: razlicite konfiguracije scene ili razlicite velicine celija / razliciti brojevi objekata

Napomena: Ovaj grafik je Platformator-specific i ne treba ga predstavljati kao cross-engine poredjenje.

Predlog mesta za figuru:

```text
Slika/Grafik 5.2 Ovde umetnuti grafik broad-phase vremena u odnosu na broj zauzetih celija.
```

#### 5.7.3. Grafik skaliranja sa brojem kolizija

Grafik 5.3. Zavisnost vremena fizicke obrade od broja kolizionih parova.

- X osa: prosecan broj kolizionih parova (`active_collision_count`, `candidate_pair_count` ili `physics_2d_collision_pairs` uz jasno navedenu definiciju)
- Y osa: prosecan `physics` time [ms]
- Serije: Platformator, Godot

Napomena: Ako se koriste razlicite metrike kolizionih parova, to mora biti jasno naglaseno u legendi ili opisu slike.

Predlog mesta za figuru:

```text
Slika/Grafik 5.3 Ovde umetnuti grafik skaliranja sa brojem kolizionih parova.
```

#### 5.7.4. Dodatni preporuceni grafikoni

Grafik 5.4. Poredjenje headless i renderovanog rezima za isti scenario.  
Grafik 5.5. Platformator interna dekompozicija vremena (`broad_phase`, `narrow_phase`, `resolve_collisions`) u odnosu na broj objekata.  
Grafik 5.6. Poredjenje box-only, circle-only i mixed scenarija ako takva merenja budu ukljucena.

### 5.8. Diskusija rezultata - sablon

U finalnoj verziji rada diskusiju rezultata preporucuje se organizovati po sledecim tackama:

1. Kratak rezime glavnog nalaza iz svake tabele i svakog grafikona.
2. Poredjenje Platformator i Godot scenarija samo nad zajednickim metrikama.
3. Posebna analiza Platformator internih metrika radi objasnjenja zasto dolazi do odredjenog rasta vremena.
4. Jasno odvajanje tvrdnji od pretpostavki. Umesto tvrdnje `uzrok je`, preporucuje se formulacija `pretpostavljamo da je glavni uzrok`.
5. Otvoreno navodjenje ogranicenja benchmark metodologije.

Predlog teksta za uvod u diskusiju:

`Prikazani benchmark rezultati pokazuju kako se Platformator ponasa pri promeni velicine i gustine scene, kao i kako se pozicionira u odnosu na Godot kao referentni 2D endzin. Posebno je vazno uociti razliku izmedju zajednickih metrika, koje sluze za cross-engine poredjenje, i internih Platformator metrika, koje sluze za objasnjenje observed trendova. Na osnovu prikazanih tabela i grafikona moguce je diskutovati ne samo apsolutne vrednosti vremena frejma, vec i odnos izmedju broja objekata, zauzetih celija i kolizionog opterecenja.`

Predlog teksta za ogranicenja:

`Pri tumacenju rezultata treba imati u vidu da Platformator i Godot ne izvestavaju potpuno iste interne metrike, kao i da njihove fizicke implementacije nisu identicne. Zbog toga se cross-engine poredjenje mora tumaciti kao poredjenje performansi nad ekvivalentnim scenarijima, a ne kao poredjenje istih internih algoritama korak po korak. Dodatno, metrika broja zauzetih celija postoji samo u Platformator sistemu i koristi se iskljucivo za internu analizu skaliranja broad-phase podsistema.`

### 5.9. Zakljucak benchmark poglavlja - sablon za finalno popunjavanje

`Na osnovu sprovedenih benchmark merenja moze se zakljuciti da Platformator [dopuniti glavni zakljucak]. Posebno je znacajno to sto [dopuniti zapazanje o skaliranju], dok je u poredjenju sa Godot sistemom primeceno da [dopuniti]. Interna analiza Platformator metrika ukazuje da [dopuniti], sto je u skladu sa ocekivanjima za sistem zasnovan na grid subdiviziji i segmentisanim intervalnim listama.`

---

## 6. Demonstracija implementiranog sistema

Demonstracija sistema moze se organizovati kao prakticna prezentacija cetiri nivoa rada Platformator okruzenja.

Prvi nivo demonstracije odnosi se na editor scena. U okviru odbrane rada kandidat moze da pokaze pokretanje desktop editora, ucitavanje scene, izbor objekta u hijerarhiji, izmenu komponenti u inspector panelu, cuvanje scene i pokretanje scene iz samog editora. Time se ilustruje da alat podrzava osnovni razvojni tok koji korisnik ocekuje od modernog gejm alata.

Drugi nivo demonstracije odnosi se na runtime izvrsavanje scene. Ovde se moze prikazati jednostavna test scena sa staticnim platformama, jednim dinamickim telom, audio izvorom i animator komponentom. Posmatrac na taj nacin direktno vidi da su scene ucitane, da se kolizije obradjuju, da animacija funkcionise i da audio sistem pravilno reaguje na dogadjaje.

Treci nivo demonstracije predstavlja Mario primer. Ovaj primer je posebno pogodan za odbranu rada zato sto istovremeno demonstrira skriptovani karakter sistema, kolizije, rad kamere, logiku platformera, resurse i scene. Kroz njega se moze pokazati da Platformator nije samo skup tehnickih modula, vec koherentna platforma na kojoj je moguce realizovati konkretnu 2D igru.

Cetvrti nivo demonstracije odnosi se na benchmark infrastrukturu. Kandidat moze da prikaze pokretanje benchmark runner-a u Platformator i Godot varijanti, kao i primer dobijenog izlaza sa scope i counter metrikama. Na odbrani nije neophodno prikazivati sva merenja, ali je korisno prikazati bar jednu kratku benchmark egzekuciju i jedan ili dva gotova grafikona iz poglavlja rezultata.

U praksi, demonstracija bi mogla teci sledecim redosledom:

1. Otvaranje editora i prikaz panela Hijerarhija, Inspector, Behaviors, Assets i Output.
2. Ucitavanje scene i kratka izmena jednog objekta ili komponente.
3. Pokretanje scene iz editora.
4. Pokretanje Mario primera i prikaz interakcije igraca sa okruzenjem.
5. Pokretanje benchmark scenarija i prikaz jednog izlaza sa mernim vrednostima.
6. Kratak osvrt na tabele, grafikone i ogranicenja sistema.

Ovakva demonstracija je vazna zato sto komisiji omogucava da vidi i alatni, i izvrsni, i eksperimentalni deo rada, sto je u ovom slucaju jedna od kljucnih vrednosti projekta.

---

## 7. Zakljucak

U ovom radu predstavljen je razvoj 2D gejm endzina Platformator sa pratecim editorom scena i fizickim podsistemom koji se oslanja na segmentisanu sweep-and-prune broad-phase detekciju kolizija. Motivacija rada bila je da se istrazivacke ideje iz oblasti optimizovane kolizione detekcije pretoce u konkretno, prakticno upotrebljivo softversko resenje koje objedinjuje scene, komponente, fiziku, resurse i alat za uredjivanje sadrzaja.

Na arhitekturalnom nivou sistem je realizovan kao kombinacija C++20 runtime jezgra i Python/PySide6 editora. Runtime pruza komponentni model, upravljanje scenama, fizicki podsistem, renderovanje, audio i skriptovanje, dok editor omogucava vizuelni rad sa scenama i integraciju sa build/run procesom. Poseban doprinos rada ogleda se u integraciji grid-subdivizije, AABB parova, segmentisanih intervalnih listi i checkpoint mehanizma u okviru sireg gejm endzin okruzenja.

Verifikacija resenja pokazala je da projektni test izvrsni programi uspesno prolaze funkcionalne provere, kao i da je sistem dovoljno kompletan da podrzi demonstracioni platformer u vidu Mario primera. Time je potvrdjeno da razvijeno resenje nije samo izolovana implementacija pojedinacnog algoritma, vec celovit prototip 2D gejm endzina sa pratecim alatima za kreiranje sadrzaja. Pored toga, rad uvodi i benchmark metodologiju koja omogucava da se sistem posmatra i kroz prizmu performansi, skaliranja i odnosa prema referentnom Godot resenju.

Istovremeno, rad je identifikovao vise pravaca daljeg razvoja. Pre svega, potrebno je dovrsiti i normalizovati benchmark merenja i na osnovu njih dopuniti tabele i grafikone predlozene u ovom dokumentu. Dalje, kontinuirana detekcija kolizija za veoma brze objekte i dodatna optimizacija solvera predstavljaju logicke algoritamske nadogradnje. Takodje, postoji prostor za dalje unapredjenje editora, paralelizacije i organizacije testnog okruzenja. Upravo ti pravci mogu da posluze kao osnova za buduci istrazivacki ili inzenjerski rad.

Moze se zakljuciti da Platformator uspesno demonstrira kako se teorijski koncepti iz oblasti sudarne detekcije, arhitekture gejm endzina i benchmark evaluacije mogu objediniti u koherentno softversko resenje. To ga cini pogodnim i kao predmet diplomskog rada i kao dalju osnovu za razvoj slozenijih interaktivnih sistema.

---

## 8. Literatura

Napomena: Spisak literature u zavrsnoj verziji rada uskladiti sa stilom citiranja koji odredi mentor ili fakultet.

[1] Tracy, D. J., Buss, S. R., Woods, B. M. Efficient Large-Scale Sweep and Prune Methods with AABB Insertion and Removal. Proceedings of the IEEE Virtual Reality Conference, 2009.

[2] Baraff, D. Dynamic Simulation of Non-Penetrating Rigid Bodies. PhD thesis, Cornell University, 1992.

[3] Cohen, J. D., Lin, M. C., Manocha, D., Ponamgi, M. K. I-COLLIDE: An Interactive and Exact Collision Detection System for Large-Scale Environments. Symposium on Interactive 3D Graphics, 1995.

[4] van den Bergen, G. Collision Detection in Interactive 3D Environments. Morgan Kaufmann, 2003.

[5] Unity Technologies. Unity Manual. Dostupno na: https://docs.unity3d.com/Manual/.

[6] Godot Engine. Official Documentation. Dostupno na: https://docs.godotengine.org/.

[7] SDL. SDL3 Documentation and Wiki. Dostupno na: https://wiki.libsdl.org/SDL3.

[8] Catto, E. Box2D Manual. Dostupno na: https://box2d.org/documentation/.

[9] Reppy, J. H., Appel, A. W., Shao, Z. Unrolling Lists. Proceedings of the 1994 ACM Conference on Lisp and Functional Programming, 1994.

[10] Platformator source code repository. Interna projektna dokumentacija i implementacija sistema. [navesti URL repozitorijuma ako je primenljivo].

[11] Eigen Documentation. Dostupno na: https://eigen.tuxfamily.org/.

[12] oneTBB Documentation. Dostupno na: https://oneapi-src.github.io/oneTBB/.

[13] Coming, D. S., Staadt, O. G. Kinetic Sweep and Prune for Multi-Body Continuous Motion. Computers and Graphics, 30(3), 439-449, 2006.

---

## 9. Biografija

<Ime i prezime kandidata> je rodjen/a <dd.mm.gggg.> godine u <mesto>. Osnovno i srednje obrazovanje zavrsio/la je u <mesto>. Skolske <20xx/20xx> godine upisuje osnovne akademske studije na Fakultetu tehnickih nauka u Novom Sadu, studijski program Softversko inzenjerstvo i informacione tehnologije. Polozio/la je sve ispite predvidjene nastavnim planom i programom i stekao/la uslov za odbranu diplomskog rada.

---

## 10. Kljucna dokumentacijska informacija

Redni broj, RBR: <uneti>  
Identifikacioni broj, IBR: <uneti>  
Tip dokumentacije, TD: monografska publikacija  
Tip zapisa, TZ: tekstualni stampani dokument  
Vrsta rada, VR: diplomski rad  
Autor, AU: <Ime i prezime kandidata>  
Mentor, MN: dr <Ime i prezime mentora>, <zvanje>  
Naslov rada, NR: Razvoj 2D gejm endzina sa editorom scena i optimizovanom broad-phase detekcijom kolizija zasnovanom na segmentisanoj sweep-and-prune metodi  
Jezik publikacije, JP: srpski  
Jezik izvoda, JI: srpski / engleski  
Zemlja publikovanja, ZP: Srbija  
Uze geografsko podrucje, UGP: Vojvodina  
Godina, GO: 2026  
Izdavac, IZ: autorski reprint  
Mesto i adresa, MA: Novi Sad, Fakultet tehnickih nauka, Trg Dositeja Obradovica 6  
Fizicki opis rada, FO: <broj poglavlja / stranica / citata / tabela / slika / grafikona / priloga>  
Naucna oblast, NO: Softversko inzenjerstvo i informacione tehnologije  
Naucna disciplina, ND: Razvoj softverskih sistema / razvoj igara / racunarska grafika  
Predmetna odrednica / kljucne reci, PO: gejm endzin, detekcija kolizija, sweep and prune, benchmark, editor scena  
UDK: <uneti>  
Cuva se, CU: Biblioteka Fakulteta tehnickih nauka, Trg Dositeja Obradovica 6, Novi Sad  
Vazna napomena, VN: <uneti ako postoji>  
Izvod, IZ:

U radu je predstavljen razvoj 2D gejm endzina Platformator sa desktop editorom scena i fizickim podsistemom za detekciju i resavanje kolizija. Jezgro sistema zasnovano je na komponentnoj arhitekturi i implementirano u programskom jeziku C++20 uz oslanjanje na SDL3, Eigen i oneTBB. Poseban fokus rada je broad-phase detekcija kolizija zasnovana na grid-subdiviziji prostora, AABB strukturama i segmentisanim intervalnim listama inspirisanim radom Tracy, Buss i Woods. Pored runtime dela sistema razvijen je i editor scena nalik Unity pristupu, koji omogucava upravljanje hijerarhijom objekata, komponentama, resursima i pokretanjem scena. Rad dodatno uvodi benchmark metodologiju i predlog poredjenja sa Godot referentnim scenarijima. Funkcionalnost sistema proverena je automatizovanim testovima i demonstracionim Mario primerom, dok su benchmark rezultati predvidjeni za naknadno popunjavanje nakon finalne normalizacije merenja.

Datum prihvatanja teme, DP: <uneti>  
Datum odbrane, DO: <uneti>  
Clanovi komisije, KO:  
predsednik: dr <Ime Prezime>, <zvanje>  
clan: dr <Ime Prezime>, <zvanje>  
mentor: dr <Ime Prezime>, <zvanje>

Potpis mentora: ____________________

---

## 11. Key words documentation

Accession number, ANO: <enter>  
Identification number, INO: <enter>  
Document type, DT: monographic publication  
Type of record, TR: textual material  
Contents code, CC: bachelor thesis  
Author, AU: <Candidate full name>  
Mentor, MN: <Mentor full name>, <title>, PhD  
Title, TI: Development of a 2D Game Engine with a Scene Editor and Optimized Broad-Phase Collision Detection Based on the Segmented Sweep-and-Prune Method  
Language of text, LT: Serbian  
Language of abstract, LA: Serbian / English  
Country of publication, CP: Serbia  
Locality of publication, LP: Vojvodina  
Publication year, PY: 2026  
Publisher, PB: author's reprint  
Publication place, PP: Novi Sad, Faculty of Technical Sciences, Trg Dositeja Obradovica 6  
Physical description, PD: <number of chapters / pages / references / tables / figures / charts / appendices>  
Scientific field, SF: Software Engineering and Information Technologies  
Scientific discipline, SD: Software Systems Engineering / Game Development / Computer Graphics  
Subject / Keywords, S/KW: game engine, collision detection, sweep and prune, benchmark, scene editor  
UDC: <enter>  
Holding data, HD: Library of the Faculty of Technical Sciences, Trg Dositeja Obradovica 6, Novi Sad  
Note, N: <enter if needed>  
Abstract, AB:

This thesis presents the development of Platformator, a 2D game engine accompanied by a desktop scene editor and a physics subsystem for collision detection and resolution. The core of the system is based on a component-oriented architecture and implemented in C++20 using SDL3, Eigen and oneTBB. A particular focus of the work is the broad-phase collision detection subsystem based on spatial grid subdivision, AABB structures and segmented interval lists inspired by the method proposed by Tracy, Buss and Woods. In addition to the runtime core, a Unity-like scene editor was developed to support hierarchy browsing, component inspection, asset management and direct scene execution. The thesis also proposes a benchmark methodology and a comparison structure with equivalent Godot scenarios. The system was validated through automated tests and a Mario-like example game, while benchmark results are left to be populated after final normalization of measurements.

Accepted by scientific board on, ASB: <enter>  
Defended on, DE: <enter>  
Defense board, DB:  
president: <Full name>, <title>, PhD  
member: <Full name>, <title>, PhD  
mentor: <Full name>, <title>, PhD

Mentor's signature: ____________________

---

## 12. Prilog A - Predlog formata sirovih benchmark podataka

Radi lakseg crtanja tabela i grafikona preporucuje se da se rezultati svakog pojedinacnog benchmark pokretanja cuvaju u tabelarnom formatu, na primer CSV ili XLSX. Jedan moguci format sirovih podataka dat je u nastavku.

Predlog kolona za cross-engine fajl:

```text
engine,scenario,mode,run_id,warmup_frames,measure_frames,dt_or_physics_fps,box_count,circle_count,
frame_avg_ms,frame_median_ms,frame_p95_ms,
physics_avg_ms,physics_median_ms,physics_p95_ms,
object_count_avg,collision_pairs_avg,notes
```

Predlog kolona za Platformator internu analizu:

```text
scenario,mode,run_id,object_count_avg,occupied_cell_count_avg,candidate_pair_count_avg,
pending_narrow_phase_pair_count_avg,active_collision_count_avg,awake_dynamic_body_count_avg,
frame_avg_ms,broad_phase_avg_ms,narrow_phase_avg_ms,resolve_collisions_avg_ms,
queued_add_count_avg,queued_sync_count_avg,queued_remove_count_avg,notes
```

Preporuceni grafikoni mogu se onda crtati iz ovih kolona na sledeci nacin:

1. `frame_avg_ms` prema `object_count_avg`
2. `broad_phase_avg_ms` prema `occupied_cell_count_avg`
3. `physics_avg_ms` prema `collision_pairs_avg` ili `active_collision_count_avg`
4. poredjenje `mode=headless` i `mode=rendered` za isti scenario

Ukoliko se rezultati budu cuvali po pojedinacnim pokretanjima, zavrsna verzija rada moze u tabelama prikazivati ili medijanu po pokretanjima ili prosek svih pokretanja, dok grafikoni mogu prikazati i error bar-ove ako mentor bude smatrao da je to pozeljno.

---