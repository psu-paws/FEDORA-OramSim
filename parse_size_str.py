import re
from types import MappingProxyType
from typing import Union

SIZE_REGEX = re.compile(r"([0-9]+(\.[0-9]*)?)\s*(?:([MmKkGgTtPp])(i)?)?B?")

SI_PREFIX_TO_ORDER = MappingProxyType({
    "K": 1,
    "M": 2,
    "G": 3,
    "T": 4,
    "P": 5,
})

def parse_size_str(size_str: Union[str, int]) -> int:
    if type(size_str) is int:
        return size_str
    
    m = re.search(SIZE_REGEX, size_str)
    if not m:
        raise RuntimeError(f"Unable to parse size str \"{size_str}\"")
    
    base = 1000
    
    number_group = m.group(1)
    decimal_group = m.group(2)
    si_prefix_group = m.group(3)
    binary_group = m.group(4)
    
    multiplier = 1
    if si_prefix_group is not None:
        base = 1000
        si_prefix = si_prefix_group.upper()
        
        if si_prefix not in SI_PREFIX_TO_ORDER:
            raise RuntimeError(f"Unknown SI prefix {si_prefix}")
        
        order = SI_PREFIX_TO_ORDER[si_prefix]
        
        if binary_group is not None:
            base = 1024
        
        
        multiplier = base ** order
        
    if decimal_group is not None:
        value = float(number_group)
        value = int(value * multiplier)
    else:
        value = int(number_group) * multiplier
    
    return value


if __name__ == "__main__":
    def parse_test(value_str: str):
        print(f"{value_str} parsed to be {parse_size_str(value_str)}")
    
    def main():
        parse_test("0")
        parse_test("10M")
        parse_test("2.5Mi")
        parse_test("4KiB")
    
    main()
