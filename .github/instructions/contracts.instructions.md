---
applyTo: '**'
---

**Role:** You are an expert software engineer focused on **Correctness**, **Local Reasoning**, and **Scalability**. You do not just write code that works; you write code that is trustworthy, self-documenting, and robust. You adhere strictly to the discipline of "Design by Contract."

## I. Core Philosophy & Mindset

* 
**Contracts are Relationships:** Treat every function call, library use, or API interaction as a formal agreement between the caller (client) and the callee (component).


* 
**Local Reasoning is Paramount:** Write code so that a reader can understand it by looking *only* at the code in front of them and its documentation, without inspecting the implementation of dependencies.


* 
**Trust through Verification:** Correctness is the first ingredient of better code. A "semi-correct" program is one that generates valid output for a specific set of valid inputs.


* **Blame Assignment:**
* If a **Precondition** is violated, it is a bug in the **Caller**.


* If a **Postcondition** is violated (despite satisfied preconditions), it is a bug in the **Callee**.





## II. Documentation Standards

**Rule:** *Every* declaration (functions, types, properties) outside a function body must be documented. Undocumented software is a liability.

### 1. The Summary Fragment

* Start every documentation comment with a summary sentence fragment.


* 
**Functions:** Describe what it *does* and what it *returns*.


* 
**Properties/Types:** Describe what it *is*.


* **Format:** End the fragment with a period. Separate it from detailed notes with a blank line.


* **Simplicity:** Omit needless words. Do not restate context already provided by the type name.



### 2. Documenting Contracts (The "Tags")

Do not rely on the user to read code to understand requirements. Use standard tags to define the contract explicitly.

* **`- Precondition:`** State requirements for the input to be valid (e.g., `i >= 0`).
* 
*Note:* Implied preconditions (like "collection must be non-empty" for `popLast`) can be omitted if obvious from the summary, but explicit is better if subtle.




* **`- Postcondition:`** State the guaranteed effects and results.
* *Note:* Rarely needed if the Summary Fragment is written correctly. Only use this if the summary cannot capture the full guarantee.




* 
**`- Invariant:`** (For types or loops) Describe conditions that are always preserved.


* 
**`- Complexity:`** Mandatory for any operation that is not constant time/space (O(1)).



**Example Template:**

```swift
/// Sorts the elements so that all adjacent pairs satisfy the [total
/// preorder](link) `areInOrder`.
///
/// - Complexity: O(N log N) comparisons.
mutating func sort(areInOrder: (T, T) -> Bool)

```



## III. API Design & Abstraction

* 
**Contracts Drive Design:** If you cannot write a simple, terse contract, the design is likely flawed. Use documentation complexity as a "smell test" to refactor code.


* 
*Example:* If a function does three things (like `realloc`), break it into three functions.


* 
**Abstraction:** Identifying a simple name for a behavior often reveals a missing abstraction. Abstracted code is easier to test and improves correctness.


* **Invariants & Encapsulation:**
* Identify "whole-program" invariants (e.g., "Every manager is an employee") and encapsulate them in specific Types (e.g., `EmployeeDatabase`) rather than scattering checks throughout the code.


* Use access control (`private`) to protect these invariants.


## IV. Handling State and Invariants

* 
**Type Invariants:** A condition that must hold true at a type's public boundary (before and after any public method call).


* 
*Construction:* Establish the invariant in `init`.


* 
*Mutation:* Functions may temporarily break the invariant internally but must restore it before returning.




* **Choosing Invariants:** Stronger invariants (e.g., `count == count`) are generally better but require stricter initializers. Weaker invariants (e.g., `count <= count`) allow more states but complicate implementation logic.



## V. Evolution and Refactoring

When changing existing code, adhere to these rules to maintain backward compatibility (Local Reasoning for the client):

1. 
**Strengthen Postconditions (Safe):** You may promise *more* (e.g., "finds the *first* occurrence" instead of "finds *an* occurrence").


2. 
**Weaken Preconditions (Safe):** You may accept *more* inputs (e.g., handling a case that previously crashed or was forbidden).


3. 
**Do NOT Weaken Postconditions (Unsafe):** Returning less specific results breaks clients relying on the old guarantee.


4. 
**Do NOT Strengthen Preconditions (Unsafe):** Requiring stricter inputs breaks clients that used to work.



## VI. Implementation Guidelines

* 
**Blame the Code, Not the Person:** When debugging, identify which contract was broken.


* 
**Use Assertions:** If a precondition is critical and cannot be checked at compile time, consider using runtime checks (like `precondition(...)` in Swift) to "fail fast" and identify the caller's bug.


* 
**Static Typing:** Leverage the type system to enforce contracts where possible (e.g., `EmployeeDatabase` implies valid manager IDs), reducing the need for written documentation.



---

### **Summary Checklist for Generating Code:**

1. 
**Naming:** Is the name simple and descriptive? 


2. **Inputs:** What are the preconditions? (Write them down) .


3. **Outputs:** What are the postconditions? (Reflect them in the summary) .


4. 
**Invariants:** Does this type maintain a consistent state?.


5. **Documentation:** Is there a summary fragment? Are Complexity and Preconditions listed?.




Here is the full text of the conference report, with code snippets formatted into Markdown blocks.

### C++ Under the Sea 2025 - Conference Report [Ambrus Toth]

**2025-11-03**
**Huawei Proprietary - Restricted Distribution**

C++ Under the Sea was organized in October 2025 this year, being the second edition of the conference in Breda, Netherlands. Listening to previous years' feedback, they extended the main conference to 2 days, added more time for breaks between talks, and incorporated a day of workshops before the talks.

I got to participate and help around the conference as a volunteer, which I always find a very rewarding experience, as we get to interact with the speakers. Apart from some logistics tasks, I could attend all talks I wanted, and even one of the workshops.

The main conference started with the keynote by Klaus Iglberger: "The Real Problem of C++". Klaus is a C++ trainer, wrote a book on C++ software design, and I got to join his workshop at the C++ On Sea conference.

In his talk, he went over the major safety problems of the language that are causing vulnerabilities and headaches, reasons why governments recommended moving away from C/C++ to memory safe languages. But after describing each problem category, he introduced a set of practices and guidelines that can mitigate the majority of such mistakes.

> "C++ doesn't give us safety nor performance by default - it gives us control over safety and performance." 
> 
> 

The sad truth is, the majority of the techniques and guidelines helping to ensure safety and correctness are underutilized in most companies, except for a small bubble of people. Some of the techniques have real costs that make them hard or infeasible to adopt, but many of them are not used simply because of not knowing, and are low-hanging fruits. Many problems are also not trivial to recognize to be root causes of unreliability in a code base unless one has heard about the problems beforehand.

Klaus already gave a version of this talk earlier; you can watch it here: `https://www.youtube.com/watch?v=vN0U4P4qmRY`.

His key recommendations were the following:

1. Use ranges, avoid raw loops 


2. Use strong types 


3. Prefer value semantics 



If everyone was aware and working towards these goals and used modern C++ guidelines, we wouldn't be having the safety conversation with governments in the first place (paraphrased from Herb Sutter). Thanks to his training 2 years ago, I've already started applying these ideas in the editor code base and I experienced the long-term benefits they bring.

The aim of this report is to collect the most impactful ideas that were presented during this conference, reinforced with references and examples I collected from earlier talks or discussions with the speakers, so that we can take prioritized actions for improving code quality and increasing developer velocity.

---

1 Use Strong Types 

The second point in the keynote focused on strong types.

1.1 Strong types for tighter function contracts 

**1.1.1 Preconditions and function parameters** 

The contract of a function is a promise that if a given set of preconditions hold before the call, the function upholds its postconditions. If those preconditions are violated, the function may choose to terminate, have undefined behavior, or do anything. Violating preconditions of functions, whether documented or implicit, are bugs.

Tangent: Runtime assertions can help discovering such bugs early on during testing, and when done at all levels of the stack, they can implicitly multiply the utility of every unit test we write after all, without adding any testing checks, the test case already tests that there were no contract violations in the covered part of the program. When combined with fuzzing, this technique can be even easier to utilize, since you don't even have to manually write the test cases. A good use case is vector's out of bounds access checks.

If preconditions cause all the trouble, it's natural to think of eliminating them. There are two ways of doing this:

1. Widening the contract by specifying the function's behavior in the case of invalid inputs.


2. Using strong types.



Widening the contract can be done by returning a Boolean, optional, or even an exception, as long as it's specified. This is useful when we expect an operation to fail, such as due to invalid user input or file system interaction. On the other hand, widening the contract can be troublesome when the sole purpose of widening is that you don't trust that the caller code is correct, but you don't want to crash the program if it still happened. Such problems make our test suite weaker (you won't catch bugs with assertions), and it will also increase your required testing effort, since whatever behavior you specified for failures, you better have tests for it.

Using strong types provides a solution to this problem. Normally, we indeed shouldn't trust our callers to be correct - we should force them. Asking them to be correct is an indication that our API is easy to use the wrong way, a.k.a. error-prone.

C++ already provides great benefits compared to a dynamically typed language like Python or JavaScript. Good JavaScript and Python libraries usually have runtime assertions for validating the type of their input arguments (those are their preconditions). Subsequently, they need to write tests for these, which is a reason why web developers like to use TypeScript.

But even in a language with an advanced type system like C++ we can easily create similarly problematic APIs if we don't use the type system to our advantage:

**Example 1.** 

```cpp
bool RenameDir(IFileManager& FileManager, BASE_NS::string path, BASE_NS::string newPath)

```

Is path a native path or a uri? *answer: must be uri* 
Is path absolute or can it also be relative?
*answer: it depends on the file manager* 

Two alternative refactorings:

```cpp
/// Moves the directory to an arbitrary new location, creating the directory if it's not already present
/// Returns true on success, the source directory will be intact and any partial copy is cleaned up
bool MoveDirectory(
    IFileManager& fileManager, 
    const alg::DirectoryURL& currentLocation, 
    const alg::DirectoryURL& destination);

/// Renames the directory, returning true on success
bool RenameDirectory(
    IFileManager& fileManager, 
    const alg::DirectoryURL& currentLocation, 
    const alg::URLDetail::Component newName);

```



It is impossible to construct an invalid DirectoryURL. It can be only constructed from its valid components or using `DirectoryURL::Parse(string_view) -> optional<DirectoryURL>` which parses absolute urls. Therefore, callers of this function are forced to explicitly mention that this is a DirectoryURL, and also handle the parsing failure before passing the argument to our function. We pushed the responsibilities outwards, keeping the API cleaner, and the implementer of the function doesn't have to be paranoid about preconditions (we don't have any).

**Example 2.** 

```cpp
BASE_NS::vector<CORE_NS::Entity> MaterialLibrary::SetMaterialUsages(shared_ptr<MaterialData> materialData)

```



* Does this function handle nullptr metadata? answer: no, it would crash. Non-null parameter was a non-documented precondition. 


* Does this take partial ownership of the metadata? *answer: no* 


* Does this mutate the metadata? *answer: no* 


* Does this mutate the this object? *answer: no* 



**Alternative refactoring:** 

```cpp
BASE_NS::vector<CORE_NS::Entity> MaterialLibrary::GetMaterialUsages(const MaterialData& materialData) const

```



Since we only borrow the value for reading, we can ask for a constant reference to an object. This requires the caller to do any pointer dereferencing/null checks, so again, handling such cases will not be the responsibility of our function. We also lifted an unnecessary constraint from the parameter type, now we no longer require MaterialMetadata to be a heap-allocated shared pointer, since we don't care about its ownership anyway.

Note: tightening the preconditions of a function is a breaking change it puts more requirements on the callers, thus we must inspect and fix all usages of our function when performing such a refactoring.

**General preconditions:** There are some general preconditions that are convenient to generally assume, unless otherwise specified:

* Arguments passed to a function by mutable reference cannot be accessed by other means during the operation.


* Arguments passed to a function by constant reference cannot be written by another means during operation.



Rust's/Swift's law of exclusivity would be nice to have here, but it's not really feasible to uphold within our current code base, therefore we should document it as a precondition if we rely on it.

**1.1.2 Postconditions and return values** 

Postconditions are properties that will be true after the function returns, given that the caller satisfied all the preconditions. Usually, postconditions are documented as part of the return value, so we don't see explicit "postcondition:" annotations.

When the caller wants to use the return value, they need to be aware of what postconditions they can rely on safely, without inspecting the function body. They have 2 sources of information: the return type, and the function's return value's specification. If something is not indicated by either the type or the specification, it's not part of the API of the function, thus we must not rely on it.

For example, if a function doesn't promise that it returns a URL with the file:/// scheme, we must not assume it does - it might change in the future. When seeing a unique_ptr returned from a function, the callee must not assume that the result is non-null, unless otherwise specified.

*Comic inserted in text depicting "Changes in version 10.17: The CPU no longer overheats when you hold down spacebar" and a user complaining this broke their workflow because they used the overheating to signal "control" key press.* *Caption: "Every change breaks someone's workflow."* 

Hyrum's law says that with enough users, all observable behaviors of a system - even unintended ones will be depended upon, regardless of the official contract. Nonetheless, if we make the contracts of our functions generally less ambiguous, we can encourage a behavior of not depending on implementation details. We must document our API contracts - either as a docstring or with a strict type.

If a function returns a unique pointer, and wants to rely on the result not being null outside without additional checks, it has 2 options: specify a postcondition as documentation, or return a `non_null<unique_ptr<T>>`. Here `non_null<T>` is a class with the invariant that any successfully constructed instance of it has a non-null (smart) pointer stored inside it.

If a function returns a url, and the caller wants to rely on that being a url within the file:/// scheme, we can either specify this as a post-condition, or return a `FileSchemed<URL>`.

Crucially, type-based postconditions compose well with type-based preconditions, and the compiler helps you guarantee that you are not making mistakes. We can often directly channel values from one function's output to another function's input, without any additional assertions because having a value of a type is already a proof of that value having certain properties - the invariants of the type.

```text
pre:
u = generateCacheUri()
post pre
post
useCacheUri(u)

```



It is up to the author of a function how much of their implementation details they want to expose in their contracts. When deciding, we can think about whether we will likely change the function later in a way that would break that property. E.g. maybe 2 months from now, I want to not only return file:/// URIs, but also project:// URIs. Relaxing a postcondition is a breaking change: we must inspect all existing usages of the function and fix each of them that relied on that property.

**1.1.3 Invariants - properties of a type** 

The invariants of a type are a set of properties that will be always true for an object of that type when accessing it through its public interface. For example, the invariant of `std::string` is that its character sequence is contiguously stored in memory, and ends with a `\0` byte. Having a `std::string_view` has slightly relaxed invariants: it only guarantees contiguous storage but doesn't guarantee null-termination (since it may be a slice to the middle of a string stored elsewhere). Relying on properties that are not promised by invariants nor preconditions is a bug, waiting to showcase itself.

Encapsulation helps with upholding invariants of a type. If there is a limited set of functions where the stored fields may get changed, we only have to worry about any tricky-to-uphold invariants in that scope. Therefore, it is encouraged to keep mutable data members private, and only modify them through member functions, which you can make sure to implement correctly.

The responsibility of a public member function is: given that the invariants hold before, it will make sure that the invariants will be upheld afterwards.

In other words:

* Precondition: invariants hold 


* Postcondition: invariants hold 



During the execution of the member function, it may temporarily break the type invariants while mutating members, but it needs to ensure that the invariants are restored before returning, and every time before we call a member function that requires valid invariants. Private member functions may not necessarily require nor uphold invariants, since they may be just helper functions that other functions use. Thus, one must be careful with calling member functions, because they may have largely different expectations of the object's state.

If you notice that your type has too many tricky invariants or too many mutating functions, and it feels like hard to always ensure the invariants are upheld, it may be a sign that you need to break down your type into several, simpler types that each have their own scope and responsibilities. Mutable state in a large scope is what we frown upon when hearing "global variables are bad", and if we created a large class with many stored fields, that is exactly what we get.

Changing the set of invariants of a type in any way is a breaking change:

* Tightening the invariants breaks the places where we create/modify the object.


* Weakening the invariants breaks the places where we consume/read the object.



Keep invariants as strict as possible. Although this puts a small burden on creating/modifying the type, the result is that after that, we gain full confidence of using all the established properties of the object. Weakening the invariants of types is a common mistake in our code base, which leads to lots of assumptions, and it's very hard to reason about when some methods are already safe to use or not.

**Failing Construction** 

The most common motivation for weakening the invariants is the lack of a standard way to achieve failing construction without exceptions. The common workaround for this is to only partially initialize the object during construction, and add a member function `Initialize() -> bool` that establishes the "strong" invariants of the object iff the function returned true. Another workaround is to encode optional semantics in our type by including an `isValid` field, and checking that field after construction to handle the error.

Both workarounds are examples of widening invariants, and they bring major harmful effects:

* The caller may forget to call Initialize after construction.


* The caller may forget to discard the object after failed initialization. (`[[nodiscard]]` annotation can help here, but doesn't solve the issue) .


* An invalid object may flow through our program to other parts of the system, which expects to work on a valid object.


* One can easily call a member function in a partially initialized state.


* Special member functions (destructor, move, copy operations) become tricky to write (unless they can be automatically synthesized). See our implementation of optional.



In his talk, Björn Fahller introduced a neat way to solve these issues.

1. Make a private constructor that just constructs the object from its already valid parts.


2. Make a public static member function that returns an `alg::optional<T>`. Call the private constructor if and only if the initialization fully succeeded.



```cpp
class LoadedPackage {
    BASE_NS::string name;
    alg::DirectoryURL basePath;

    explicit LoadedPackage(BASE_NS::string&& name, alg::DirectoryURL&& basePath)
        : name(BASE_NS::move(name)), basePath(BASE_NS::move(basePath)) {}

public:
    static alg::optional<LoadedPackage> Load(alg::DirectoryURL baseUrl) {
        // ... parsing Logic
        if (error) {
            return alg::nullopt;
        }
        return LoadedPackage(BASE_NS::move(name), BASE_NS::move(baseUrl));
    }
};

```



If the created object must be a heap-allocated unique_ptr/shared_ptr, we cannot keep the constructor private because it has to be accessible within make_unique / make_shared. Luckily, the passkey idiom can help us make the constructor effectively private :

```cpp
class LoadedPackage {
    BASE_NS::string name_;
    alg::DirectoryURL basePath_;

    struct Passkey {};   // Private passkey

public:
    explicit LoadedPackage(Passkey, BASE_NS::string&& name, alg::DirectoryURL&& basePath)
        : name_(BASE_NS::move(name)), basePath_(BASE_NS::move(basePath)) {}

    static BASE_NS::unique_ptr<LoadedPackage> Load(alg::DirectoryURL baseUrl) {
        // ... parsing Logic
        if (error) {
            return nullptr;
        }
        return BASE_NS::make_unique<LoadedPackage>(Passkey{}, BASE_NS::move(name), BASE_NS::move(baseUrl));
    }
};

```



This makes the constructor effectively private because nobody, except the factory method has access to the Passkey struct, so only it can provide the necessary argument to call the constructor.

1.2 Strong types for fixing the C heritage 

Types are a great tool for mitigating classes of bugs from programs. Unfortunately, implicit conversions of basic C++ types can allow misusing APIs in very unexpected ways. For example, pointers are implicitly convertible to bool, so we can easily pass a string literal to a function that expects bool, and let it pass type checking. One doesn't make this mistake usually when they first write a function call, because they see the parameter types in their editor. This is something that happens over time, as the API gets refactored, parameters get swapped around, deleted and inserted.

We can and should try to detect such errors with compiler warnings and static analysis, but a more robust solution is to use a wrapper type that prevents implicit conversions.

Cool technique for preventing any kind of implicit conversion: make a function template and constrain its parameter types with `std::same_as<T, intended type>`.

```cpp
class Index {
    size_t value_;

public:
    template<typename T, typename = BASE_NS::enable_if_t<BASE_NS::is_same_v<T, size_t>>>
    [[nodiscard]] constexpr explicit Index(T rawValue) : value_(rawValue) {}

    [[nodiscard]] constexpr size_t Value() const
    {
        return value_;
    }

    friend constexpr bool operator==(const Index& lhs, const Index& rhs)
    {
        return lhs.value_ == rhs.value_;
    }

    friend constexpr bool operator!=(const Index& lhs, const Index& rhs)
    {
        return !(lhs == rhs);
    }
};

```



This prevents any kind of implicit conversion! 

---

2 Aim for Local Reasoning 

Local reasoning is the idea that the reader can make sense of the code directly in front of them, without going on a journey discovering how the code works.

**2.1 Motivation** 

```cpp
// BASE_NS::string dirUri
// ...
editor.GetFileManager();
if (separateDir) {
    // ...
    // Check if directory exists
    if (fileManager.CreateDirectory(dirUri)) {
        CORE_LOG_O("Directory Created");
    } else {
        // ...
        dirUri = project->GetAssetUri(name, category);
        // ...
    }
}

```



public method in class `IProject`:

```cpp
virtual Base::string GetAssetUri(Base::string_view name, Base::string_view category) const = 0;

```



Does `GetAssetUri()` guarantee that the returned uri will end in a `/` so that `dirUri` concatenation is valid? *answer: The GetAssetUri function was never written to return directory URIs, it's supposed to return file URIs according to its function name.* 

The code happens to work due to implementation details of the function we relied on. Maintaining the GetAssetUri API will be a headache, since it gave opportunity for its callers to misuse it. Having non-local reasoning slows down development, as the programmer needs to read and understand a lot of code to get to be able to modify or debug the code. These systems can be described as brittle, accidentally working, and most likely, unreliable on the long term.

**2.2 Simple ways to ensure local reasoning** 

* Name your symbols well, in a way that helps the programmer understand what it does on the call-site. Kate Gregory "Naming is Hard: Let's Do Better".


* Use strong types and accurate function APIs (described in Section 1).


* Prefer value semantics (Section 3).


* Document each declaration in the sources so callers won't need to look at the source code to understand what it's doing, just hover over the symbol in the IDE.



Example guideline, taken from the Better Code book's draft:

* Every declaration outside a function body must have a documentation comment that describes its contract.


* Start with a summary sentence fragment.


* Describe what a function or method does and what it returns.


* Describe what a property or type is.


* Separate the fragment from any additional documentation with a blank line and end it with a period.




* Preconditions, postconditions and invariants obviously implied by the summary need not be explicitly documented.


* Declarations that fulfill protocol requirements are exempted when nothing useful can be added to the documentation of the protocol requirement itself.


* Document the performance of every operation that doesn't execute in constant time and space.



The greatest low-hanging fruit is documenting the `BASE_NS` and `META_NS` data structures, and other, widely reused utilities. I had multiple experience with such undocumented functions doing different things that I expected, e.g. have additional preconditions.

3 Prefer Value Semantics 

* Klaus Iglberger: C++ Value Semantics 


* Dave Abrahams: Safety, Independence, Projection, & Future of Programming 



Value semantics is great for local reasoning (mitigates lifetime & race condition problems). Less memory allocations and indirections.

* Have reference semantics only in function arguments, don't store references.


* Const correctness becomes easy on value types, const is a deep const.



Reference semantics comes with:

* Spooky action at a distance 


* Incidental algorithms 


* Possible reentrant action while invariants are broken 



*Diagram Text:* Reference semantics | visibly broken invariant: Technical debt, Spooky action, Incidental algorithms, reentrant access. 

* Fundamentally hostile to concurrency 


* Surprise mutation 


* 2 main safety problems: Lifetime safety, Race conditions 



---

4 Use Ranges; avoid raw loops 

C++ ranges and views are abstractions of generic, composable algorithms that can make application code easier to read, more correct and often more efficient. At the same time, they are the single most underappreciated technique in the industry. People often don't think they are writing algorithms, just some simple for loops. But in fact, many patterns already have a name, we just don't know them yet - thus it's hard to see we are using them all the time.

Screenshot from Klaus keynote:
"If you want to improve code quality in your organization, I would say, take all your coding guidelines and replace them with the one goal. That's how important I think this one goal is: **No Raw Loops.** This will make the biggest change in code quality within your organization." 

(Sean Parent, C++ Seasoning, Going Native 2013) [Sean Parent: Principal scientist & software architect in Adobe's Software Technology Lab, previously working on Photoshop & Lightroom from 1993] See the referenced talk: `https://www.youtube.com/watch?v=W2tWOdzgXHA` 

**Bounds Safety: An Example** 

```cpp
void print_five_biggest_countries()
{
    std::vector<Continent> continents = get_continents_with_countries();
    std::vector<Country> countries;
    for( auto const& continent : continents) {
        for( auto const& country : continent.countries)
        {
            auto pos = begin(countries);
            while( pos != end(countries) && pos->area > country.area)
                ++pos;
            countries.insert( pos, country);
        }
    }

    std::vector<Country> five_biggest_countries{ begin(countries), begin(countries)+5 };
    
    for( size_t i=0; i<4; ++i)
    {
        auto& country1 = five_biggest_countries[i];
        for( size_t j=i+1; j<5; ++j) {
            auto& country2 = five_biggest_countries[j];
            if( country1.residents < country2.residents) {
                Country tmp{ country1 };
                country1 = country2;
                country2 = tmp;
            }
        }
    }
}

```



**Bounds Safety: An Improved Example** 

```cpp
struct Continent
{
    std::string name; // Name of the continent
    std::vector<Country> countries; // Countries of the continent
};

void print_five_biggest_countries()
{
    std::vector<Continent> continents = get_continents_with_countries();
    
    auto countries = continents 
        | std::views::transform(to_countries())
        | std::views::join
        | std::ranges::to<std::vector>();

    std::ranges::sort( countries, is_larger());

    auto five_biggest_countries = countries | std::views::take(5);
    std::ranges::sort( five_biggest_countries, is_more_populated());

    for( auto const& country : five_biggest_countries) {
        std::cout << country << '\n';
    }
}

```



The "Ranges" style:

* Much shorter 
* Simpler 
* Faster 
* No manual handling of indices/iterators 
* No bounds safety problem! 

**Safer:** Bounds errors, like accessing an element at a non-existing index, off-by-one errors and iterator invalidation errors are very common sources of bugs, leading to buffer overrun vulnerabilities and correctness problems in edge cases. Range based algorithms sidestep such problems, as they don't expose raw iterators or indices to the application code.

**Easier to read:** You only need to understand a named algorithm once. After that, it becomes part of your vocabulary and intuition, just like calculus. In case of an unfamiliar algorithm, access its documentation in your IDE by hovering over its name. While browsing our code base, I often find a hidden algorithm within a large 100-line function, whose implementation is scattered around on multiple screens, thus it's hard to recognize unless you carefully read the whole function. When I refactor such code to use an algorithm, I usually decrease the number of variables in the scope, and reduce code size, making it easier to follow.

**Capable of doing more advanced stuff:** The power of abstractions is that they allow you to think about problems on a higher level in a simpler way. I wouldn't have been able to implement the real-time synchronization and migration system for the shader graph if I didn't think in terms of filters, partitionings and transformations.

**Express your thoughts faster:** once you know which algorithm you want to apply, it's usually quicker and shorter to apply it than writing the loop-based alternative. I especially find `filter`, `map`, `all_of`, `none_of`, `any_of` and `find_if` algorithms common time-savers.

**Views: efficient algorithms done lazily:** Oftentimes, we want to expose a simple API for our class to access a list of its internal elements. To make this work without extra allocations, we typically use an `array_view<T>` or expose the underlying collection with a const reference directly (e.g. return `const& vector<T>`). However, when we need to apply some transformations on this collection, we usually need to resort to making a copy of the whole collection and perform multiple passes for each of our transformations. Views allow us to chain together operations (view adaptors) like:

```cpp
auto res = myVector() | alg::filter(alg::even)
                      | alg::map(alg::square)
                      | alg::sum;

```



and compile them down to an efficient, one-pass fused loop, without allocations, which would have been significantly trickier to write manually. These work by creating adaptor iterator types that capture the original iterators and do the necessary work lazily, whenever the iterator is incremented/dereferenced.


Other notes:
- Conditions that are evident from the parameter type's invariants need not be documented as function preconditions.