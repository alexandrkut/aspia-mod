Aspia
=====
Remote desktop, file transfer and system information tool.

Этот форк
---------

Форк Aspia 3.2.2. Изменения:

- **Подключение через командную строку.**  
  Ключ `--connect` (он же `-c`) принимает ID записи адресной книги, ID хоста или адрес
  хоста. Логин и пароль для этой сессии задаются ключами `--username` и `--password`,
  вид сессии — ключом `--session-type` (`desktop`, `file`, `info`, `chat`, `terminal`).
  Хост, названный по адресу, подключается напрямую, без роутера. Запрос от второй копии
  клиента передаётся уже запущенной, а не открывает второе окно.

- **Отключение 2FA для каждого типа доступа.**  
  В конфиге роутера 2FA выключается отдельно для администратора, менеджера и оператора:

      [router]
      two_factor_admin=0
      two_factor_manager=0
      two_factor_operator=0

  Роль с выключенным переключателем входит без кода, и каждый такой вход пишется в лог
  роутера. По умолчанию 2FA включена для всех трёх ролей.

- **Удалённое управление хостом через роутер.** *(только для пользователей роутера с ролью ADMIN)*  
  Пользователи, настройки обновлений, полный конфиг из файла, массовые операции с этими
  функциями (с отчётом по выполнению).

- **Четвёртый блок версии в списке хостов.**  
  Хост кладёт полную строку версии (`3.2.2.1-mod`) в телеметрию, роутер показывает её в
  колонке «Версия». Сравнения версий и рукопожатие не тронуты.

- **Экспорт инсталлера хоста с перезаписью настроек.**  
  При экспорте можно включить «Затирать старые данные при установке». Тогда настройки
  хоста (адрес роутера, пользователи, параметры доступа) перезаписываются данными из
  инсталлера и поверх уже стоящего хоста; без галочки они переносятся только при первой
  установке.

- **Экспорт Quick Support с пользователями хоста.**  
  Галочка «Выгружать пользователей этого хоста» кладёт в пакет пользователей: на такой
  хост можно войти своим логином и паролем, а не только по одноразовому. Пароли в пакет
  не попадают — только соль и проверочное значение.

- **Автоматическое обновление отключено.**  
  Проверку можно включить, но при появлении новой версии скачаются оригинальные пакеты:
  сервер обновлений оригинала ничего не знает о моде. Сервер обновления мода будет
  запущен позже.

- **Иконка хоста на рабочем столе не создаётся.**  
  Галочка «Создать на рабочем столе» в инсталлере хоста не отмечена по умолчанию; ярлык
  в меню «Пуск» остаётся.

- **Инсталлеры ставятся поверх любой версии.**  
  Пакет удаляет ранее установленную копию с тем же кодом обновления — и оригинальную, и
  мод другой линии версий, — поэтому в списке установленных программ остаётся одна
  запись. Повторный запуск уже установленного пакета переустанавливает его, а не
  предлагает «Изменить, восстановить или удалить». В названии продукта видна отметка
  `(mod)`, версия остаётся числом.

- **Роутер для Debian 10.**  
  Собирается без частей, которые рисуют: без Qt Gui и виджетов, без кодеков и X11.
  Требует только `libdbus-1-3` и glibc 2.28; C++ runtime вкомпилирован статически.

**В каком пакете изменение:**

| Изменение | Клиент | Хост | Роутер |
|:--|:--:|:--:|:--:|
| Подключение через командную строку | + | | |
| Отключение 2FA для каждого типа доступа | | | + |
| Удалённое управление хостом | + | + | + |
| Четвёртый блок версии в списке хостов | + | + | + |
| Экспорт инсталлера хоста с перезаписью настроек | | + | |
| Экспорт Quick Support с пользователями хоста | + | + | |
| Отключение автоматического обновления | + | + | |
| Иконка хоста на рабочем столе не создаётся | | + | |
| Инсталлеры ставятся поверх любой версии | + | + | + |

**Чего в этой линии нет:** поддержки хостов версии 2.7 — она осталась в линии 3.1
(клиент `3.1.2.13-mod`, роутер и relay линейки 3.0). Хосты 2.7 подключаются только
клиентом 3.1.2.13-mod через `Aspia Router 3.0.37.2-mod` и relay линейки 3.0.

**Несколько мониторов одновременно** — в 3.2.2 это штатное средство программы (экран
всего рабочего стола), отдельной правки мода для него больше нет.

Где это в интерфейсе:

- Подключение из командной строки — ключ `--connect` с ID записи адресной книги, с ID
  хоста или с его адресом; логин с паролем — `--username` и `--password`, вид сессии —
  `--session-type`.
- Управление хостом через роутер — правый клик по хосту во вкладке «Управление»:
  «Настройка обновлений хоста...», «Пользователи хоста...», «Полный конфиг хоста (из
  файла)...». При выборе нескольких хостов — те же операции разом («Добавить юзера на
  хосты...» и так далее) с превью, матрицей результатов и отменой.
- Экспорт — кнопка «Экспорт» в окне настроек хоста: «Установщик» (с галочкой
  «Затирать старые данные при установке») и «Быстрая поддержка» (с галочкой
  «Выгружать пользователей этого хоста»).
- Отключение 2FA по ролям — конфиг роутера, секция `[router]`, ключи
  `two_factor_admin`, `two_factor_manager`, `two_factor_operator`.

Обновление
----------

Автоматическое обновление отключено. При включении скачиваются оригинальные обновления.

With Aspia, you can create your own NAT traversal infrastructure (using Router and Relay servers) with connection by ID or use direct connections. Aspia supports many features. Among them, detailed information about the system, terminal, audio, text chat.

|Build Status|
|:--:|
|[![Build status](https://github.com/dchapyshev/aspia/actions/workflows/windows.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/windows.yml) [![Build status](https://github.com/dchapyshev/aspia/actions/workflows/linux.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/linux.yml) [![Build status](https://github.com/dchapyshev/aspia/actions/workflows/macos.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/macos.yml) [![Build status](https://github.com/dchapyshev/aspia/actions/workflows/android.yml/badge.svg?branch=master)](https://github.com/dchapyshev/aspia/actions/workflows/android.yml)|

Currently supported
-------------------
- Remote desktop management
- File transfer
- Remote terminal
- System information
- Text chat
- Task manager
- Encryption
- Authorization (it is possible to add users with different access rights)
- Address book with encryption and master-password
- <b>NAT traversal with connection by ID</b> (with using Aspia Router and Aspia Relay)
- Direct connections
- Audio support
- Video recording
- Host and Client for Windows, MacOSX, Linux and Android
- Router/Relay for Windows and Linux
- And much more

System requirements
-------------------
- Windows 7/2008 R2 or higher (x86 or x86_64 CPU)
- Ubuntu 22.04 / RHEL 8.10 (x86_64 CPU)
- MacOSX (x86_64 or ARM64 CPU)
- Android 12+

Contacts
--------
E-Mail: dmitry@aspia.ru

Group in Telegram: [@aspia_talks](https://t.me/aspia_talks)

News in Telegram: [@aspia_news](https://t.me/aspia_news)

Licensing
---------
Project code is available under the GNU General Public License 3.

For more information, see [license agreement](LICENSE.md).

See also
--------
- [Documentation](https://aspia.org/documentation.html)
- [Instructions for building the project (Windows)](https://aspia.org/docs/building-windows)
- [Instructions for building the project (Linux)](https://aspia.org/docs/building-linux)
- [Instructions for building the project (MacOSX)](https://aspia.org/docs/building-macos)
- [Instructions for translators](https://aspia.org/docs/translators)
- [Code of conduct](CODE_OF_CONDUCT.md)
